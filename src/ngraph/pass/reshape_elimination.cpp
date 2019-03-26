//*****************************************************************************
// Copyright 2017-2019 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//*****************************************************************************

#include "reshape_elimination.hpp"
#include <algorithm>
#include <iostream>
#include <numeric>
#include <unordered_set>

#include "ngraph/graph_util.hpp"
#include "ngraph/log.hpp"
#include "ngraph/op/add.hpp"
#include "ngraph/op/broadcast.hpp"
#include "ngraph/op/broadcast.hpp"
#include "ngraph/op/dot.hpp"
#include "ngraph/op/parameter.hpp"
#include "ngraph/op/reshape.hpp"
#include "ngraph/pattern/matcher.hpp"
#include "ngraph/pattern/op/label.hpp"
#include "ngraph/pattern/op/skip.hpp"
#include "ngraph/util.hpp"

using namespace std;
using namespace ngraph;

extern template AxisVector ngraph::apply_permutation<AxisVector>(AxisVector input,
                                                                 AxisVector order);

void pass::ReshapeElimination::construct_identity_reshape_pattern()
{
    Shape shape_op{3};
    Shape shape_r1{1, 3};

    auto op = make_shared<pattern::op::Label>(element::f32, shape_op);
    auto reshape1 = make_shared<op::Reshape>(op, AxisVector{0}, shape_r1);

    auto callback = [op](pattern::Matcher& m) {
        NGRAPH_DEBUG << "In callback for construct_identity_reshape_pattern against node = "
                     << m.get_match_root()->get_name();
        auto pattern_map = m.get_pattern_map();
        auto gop = pattern_map[op];

        auto r1 = dynamic_pointer_cast<op::Reshape>(m.get_match_root());

        if (r1->get_shape() != gop->get_shape())
        {
            NGRAPH_DEBUG << "Not a no-op; Shapes are different!";
            return false;
        }

        auto do_r1 = get_default_order(r1->get_shape());

        if (do_r1 != r1->get_input_order())
        {
            NGRAPH_DEBUG << "Not a no-op; Not in default input order!";
            return false;
        }

        replace_node(m.get_match_root(), gop);
        return true;
    };

    auto m = make_shared<pattern::Matcher>(reshape1, callback);
    this->add_matcher(m);
}

void pass::ReshapeElimination::construct_reshapex2_pattern()
{
    Shape shape_op{3};
    Shape shape_r1{1, 3};

    auto op = make_shared<pattern::op::Label>(element::f32, shape_op);
    auto reshape1 = make_shared<op::Reshape>(op, AxisVector{0}, shape_r1);
    auto reshape2 = make_shared<op::Reshape>(reshape1, AxisVector{0, 1}, shape_op);

    auto callback = [op](pattern::Matcher& m) {
        NGRAPH_DEBUG << "In callback for construct_reshapex2_pattern against node = "
                     << m.get_match_root()->get_name();
        auto pattern_map = m.get_pattern_map();

        auto gop = pattern_map[op];

        if (gop->get_shape() != m.get_match_root()->get_shape())
        {
            NGRAPH_DEBUG << "Operand shape doesn't match the shape of the second reshape!";
            NGRAPH_DEBUG << "gop " << gop->get_name()
                         << "shape = " << vector_to_string(gop->get_shape());
            NGRAPH_DEBUG << "match_root " << m.get_match_root()->get_name()
                         << "shape = " << vector_to_string(m.get_match_root()->get_shape());
            return false;
        }

        auto r2 = dynamic_pointer_cast<op::Reshape>(m.get_match_root());
        auto r1 = dynamic_pointer_cast<op::Reshape>(r2->get_argument(0));

        auto do_r2 = get_default_order(r1->get_shape());
        auto do_r1 = get_default_order(gop->get_shape());

        NGRAPH_DEBUG << "r1's i/o = " << vector_to_string(r1->get_input_order())
                     << "do_r1 = " << vector_to_string(do_r1);
        NGRAPH_DEBUG << "r2's i/o = " << vector_to_string(r2->get_input_order())
                     << "do_r2 = " << vector_to_string(do_r2);

        if (r1->get_input_order() == do_r1 && r2->get_input_order() == do_r2)
        {
            NGRAPH_DEBUG << "Two reshapes were removed!";
            replace_node(m.get_match_root(), gop);
            return true;
        }

        auto perm1 = apply_permutation(do_r1, r1->get_input_order());
        auto perm2 = apply_permutation(perm1, r2->get_input_order());
        if (perm2 == do_r1)
        {
            NGRAPH_DEBUG << "Two transposes were removed!";
            replace_node(m.get_match_root(), gop);
            return true;
        }

        return false;
    };
    auto m = make_shared<pattern::Matcher>(reshape2, callback);
    this->add_matcher(m);
}

void pass::ReshapeElimination::construct_dot_transpose_pattern()
{
    // dot(A,B).T = dot (B.T, A.T)
    auto dot_pred = [](shared_ptr<Node> n) {
        return static_cast<bool>(dynamic_pointer_cast<op::Dot>(n));
    };

    auto pdot = make_shared<pattern::op::Label>(element::f32, Shape{2, 1}, dot_pred);
    auto preshape = make_shared<op::Reshape>(pdot, AxisVector{1, 0}, Shape{1, 2});

    pattern::graph_rewrite_callback callback = [](pattern::Matcher& m) {
        NGRAPH_DEBUG << "In callback for construct_dot_transpose_pattern against node = "
                     << m.get_match_root()->get_name();

        auto mtranspose = static_pointer_cast<op::Reshape>(m.get_match_root());
        // this also checks the rank
        if (mtranspose->get_input_order() != AxisVector{1, 0})
        {
            NGRAPH_DEBUG << "Reshape isn't transpose. "
                         << vector_to_string(mtranspose->get_input_order());
            return false;
        }

        auto mdot = mtranspose->get_argument(0);
        if (mdot->get_shape().size() != 2)
        {
            NGRAPH_DEBUG << "Dot has the wrong shape. " << vector_to_string(mdot->get_shape());
            return false;
        }

        auto arg0 = mdot->get_argument(0);
        if (arg0->get_shape().size() != 2)
        {
            NGRAPH_DEBUG << "Arg0 has the wrong shape. " << vector_to_string(arg0->get_shape());
            return false;
        }
        auto reshape0_shape = Shape{arg0->get_shape().at(1), arg0->get_shape().at(0)};
        auto reshape0 = make_shared<op::Reshape>(arg0, AxisVector{1, 0}, reshape0_shape);

        auto arg1 = mdot->get_argument(1);
        if (arg1->get_shape().size() != 2)
        {
            NGRAPH_DEBUG << "Arg1 has the wrong shape. " << vector_to_string(arg1->get_shape());
            return false;
        }
        auto reshape1_shape = Shape{arg1->get_shape().at(1), arg1->get_shape().at(0)};
        auto reshape1 = make_shared<op::Reshape>(arg1, AxisVector{1, 0}, reshape1_shape);

        auto tdot = shared_ptr<Node>(new op::Dot(reshape1, reshape0));
        replace_node(m.get_match_root(), tdot);
        return true;
    };

    auto m = make_shared<pattern::Matcher>(preshape, callback);
    this->add_matcher(m);
}

void pass::RecurrentReshapeElimination::construct_recurrent_reshape()
{
    Shape shape_op{3};
    Shape shape_r{1, 3};

    auto no_fan_out = [](std::shared_ptr<Node> n) {
        auto users = n->get_users(true);
        std::set<std::shared_ptr<Node>> user_set(users.begin(), users.end());
        size_t num_unique_users = user_set.size();
        if (num_unique_users == 1)
        {
            return true;
        }
        else
        {
            NGRAPH_DEBUG << "recurrent_reshape_elimination: " << n->get_name() << " has fan out\n";
            return false;
        }

    };

    auto op = make_shared<pattern::op::Label>(element::f32, shape_op);
    auto reshape = make_shared<op::Reshape>(op, AxisVector{0}, shape_r);
    auto rehape_label = make_shared<pattern::op::Label>(reshape, nullptr, NodeVector{reshape});

    auto callback = [op, rehape_label](pattern::RecurrentMatcher& m) {
        const auto sequence_len = m.get_number_of_recurrent_matches();
        auto reshape_node_vector = m.get_bound_nodes_for_pattern(rehape_label);
        std::cout << sequence_len << std::endl;
        for (auto it : reshape_node_vector)
        {
            std::cout << it->get_name() << " ";
        }
        std::cout << std::endl;
        auto driver_op = reshape_node_vector.back()->get_argument(0);
        auto last_bounded_reshape_op = reshape_node_vector.front();
        std::cout << driver_op->get_name() << " " << last_bounded_reshape_op->get_name()
                  << std::endl;
        replace_node(last_bounded_reshape_op, driver_op);
        return true;

    };
    std::set<std::shared_ptr<pattern::op::Label>> empty_correlated_matches;
    auto m = std::make_shared<pattern::RecurrentMatcher>(
        rehape_label, op, empty_correlated_matches, callback);
    this->add_matcher(m);
}
