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

// Contains logic derived from TensorFlow’s bfloat16 implementation
// https://github.com/tensorflow/tensorflow/blob/d354efc/tensorflow/core/lib/bfloat16/bfloat16.h
// Copyright notice from original source file is as follows.

//*******************************************************************************
//  Copyright 2016 The TensorFlow Authors. All Rights Reserved.
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//    http://www.apache.org/licenses/LICENSE-2.0
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.
//==============================================================================

#include <cmath>
#include <iostream>

#include "ngraph/type/bfloat16.hpp"

using namespace std;
using namespace ngraph;

bool float_isnan(const float& x)
{
    return std::isnan(x);
}

std::vector<float> bfloat16::to_float_vector(const std::vector<bfloat16>& v_bf16)
{
    std::vector<float> v_f32(v_bf16.begin(), v_bf16.end());
    return v_f32;
}

std::vector<bfloat16> bfloat16::from_float_vector(const std::vector<float>& v_f32)
{
    std::vector<bfloat16> v_bf16(v_f32.size());
    for (float a : v_f32)
    {
        v_bf16.push_back(static_cast<bfloat16>(a));
    }
    return v_bf16;
}

ngraph::bfloat16 bfloat16::from_bits(uint16_t bits)
{
    bfloat16 rc;
    rc.m_value = bits;
    return rc;
}

std::string bfloat16::to_string() const
{
    return std::to_string(static_cast<float>(*this));
}

size_t bfloat16::size() const
{
    return sizeof(m_value);
}

bool bfloat16::operator==(const bfloat16& other) const
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"
    return (static_cast<float>(*this) == static_cast<float>(other));
#pragma clang diagnostic pop
}

bool bfloat16::operator<(const bfloat16& other) const
{
    return (static_cast<float>(*this) < static_cast<float>(other));
}

bool bfloat16::operator<=(const bfloat16& other) const
{
    return (static_cast<float>(*this) <= static_cast<float>(other));
}

bool bfloat16::operator>(const bfloat16& other) const
{
    return (static_cast<float>(*this) > static_cast<float>(other));
}

bool bfloat16::operator>=(const bfloat16& other) const
{
    return (static_cast<float>(*this) >= static_cast<float>(other));
}

bfloat16::operator float() const
{
    uint32_t tmp = (static_cast<uint32_t>(m_value) << 16);
    const float* f = reinterpret_cast<const float*>(&tmp);
    return *f;
}

bfloat16::operator double() const
{
    return static_cast<float>(m_value);
}

bfloat16::operator uint16_t() const
{
    return m_value;
}
