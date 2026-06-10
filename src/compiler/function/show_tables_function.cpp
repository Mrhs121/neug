/**
 * Copyright 2020 Alibaba Group Holding Limited.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "neug/compiler/function/show_tables_function.h"
#include <glog/logging.h>
#include "neug/execution/common/columns/value_columns.h"
#include "neug/execution/common/context.h"
#include "neug/storages/graph/schema.h"
#include "neug/utils/exception/exception.h"

namespace neug {
namespace function {

function_set ShowTablesFunction::getFunctionSet() {
  auto function = std::make_unique<NeugCallFunction>(
      ShowTablesFunction::name,
      std::vector<neug::common::LogicalTypeID>{},
      std::vector<std::pair<std::string, neug::common::LogicalTypeID>>{
          {"name", neug::common::LogicalTypeID::STRING},
          {"type", common::LogicalTypeID::STRING}});

  function->bindFunc = [](const neug::Schema& schema,
    const neug::execution::ContextMeta& ctx_meta,
    const ::physical::PhysicalPlan& plan,
    int op_idx) -> std::unique_ptr<CallFuncInputBase> {
    return std::make_unique<ShowTablesFuncInput>();
  };

  function->execFunc = [](const CallFuncInputBase& input, neug::IStorageInterface& graph) {
    try {
      neug::execution::Context ctx;
      const auto& schema = graph.schema();

      neug::execution::ValueColumnBuilder<std::string> name_builder;
      neug::execution::ValueColumnBuilder<std::string> type_builder;

      // Count total rows for reserve
      const auto& vertexSchemas = schema.get_all_vertex_schemas();
      const auto& edgeSchemas = schema.get_all_edge_schemas();
      size_t totalRows = 0;
      for (const auto& vs : vertexSchemas) {
        if (vs && !schema.is_vertex_label_soft_deleted(vs->label_name)) {
          totalRows++;
        }
      }
      for (const auto& [_, es] : edgeSchemas) {
        if (es && !schema.is_edge_label_soft_deleted(es->src_label_name,
                                                      es->dst_label_name,
                                                      es->edge_label_name)) {
          totalRows++;
        }
      }

      name_builder.reserve(totalRows);
      type_builder.reserve(totalRows);

      // Vertex tables
      for (const auto& vs : vertexSchemas) {
        if (!vs) continue;
        if (schema.is_vertex_label_soft_deleted(vs->label_name)) continue;
        name_builder.push_back_opt(vs->label_name);
        type_builder.push_back_opt("NODE");
      }

      // Edge tables
      for (const auto& [_, es] : edgeSchemas) {
        if (!es) continue;
        if (schema.is_edge_label_soft_deleted(es->src_label_name,
                                               es->dst_label_name,
                                               es->edge_label_name)) {
          continue;
        }
        name_builder.push_back_opt(es->edge_label_name);
        type_builder.push_back_opt("REL");
      }

      ctx.set(0, name_builder.finish());
      ctx.set(1, type_builder.finish());
      ctx.tag_ids = {0, 1};
      return ctx;
    } catch (const std::exception& e) {
      LOG(ERROR) << "ShowTables failed: " << e.what();
      THROW_RUNTIME_ERROR("ShowTables failed: " + std::string(e.what()));
    }
  };

  function_set functionSet;
  functionSet.push_back(std::move(function));
  return functionSet;
}

}  // namespace function
}  // namespace neug
