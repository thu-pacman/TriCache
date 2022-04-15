// Copyright 2022 Guanyu Feng, Tsinghua University
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once
#include "type.hpp"
#include <tuple>

namespace scache
{
    class RoundRobinPartitioner
    {
    public:
        RoundRobinPartitioner(partition_id_type _num_partitions, vpage_id_type _num_vpages)
            : num_partitions(_num_partitions), num_vpages(_num_vpages)
        {
        }

        std::tuple<partition_id_type, block_id_type> operator()(vpage_id_type vpage_id) const
        {
            return {vpage_id % num_partitions, vpage_id / num_partitions};
        }

        block_id_type num_blocks(partition_id_type partition_id) const
        {
            return num_vpages / num_partitions + (partition_id < num_vpages % num_partitions);
        }

        vpage_id_type operator()(partition_id_type partition_id, block_id_type block_id) const
        {
            return block_id * num_partitions + partition_id;
        }

    private:
        const partition_id_type num_partitions;
        const vpage_id_type num_vpages;
    };

} // namespace scache
