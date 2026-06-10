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

#include "neug/compiler/function/table_info_function.h"
#include <glog/logging.h>
#include "neug/execution/common/columns/value_columns.h"
#include "neug/execution/common/context.h"
#include "neug/storages/graph/schema.h"
#include "neug/utils/exception/exception.h"

namespace neug {
namespace function {

function_set TableInfoFunction::getFunctionSet() {
  auto function = std::make_unique<NeugCallFunction>(
      TableInfoFunction::name,
      std::vector<neug::common::LogicalTypeID>{
          neug::common::LogicalTypeID::STRING},
      std::vector<std::pair<std::string, neug::common::LogicalTypeID>>{
          {"property_name", common::LogicalTypeID::STRING},
          {"property_type", common::LogicalTypeID::STRING},
          {"is_primary_key", common::LogicalTypeID::STRING},
          {"src", common::LogicalTypeID::STRING},
          {"dst", common::LogicalTypeID::STRING}});

  function->bindFunc = [](const neug::Schema& schema,
    const neug::execution::ContextMeta& ctx_meta,
    const ::physical::PhysicalPlan& plan,
    int op_idx) -> std::unique_ptr<CallFuncInputBase> {
    std::string tableName;
    auto procedurePB = plan.plan(op_idx).opr().procedure_call();
    const auto& args = procedurePB.query().arguments();
    if (args.size() > 0 && args[0].has_const_()) {
      const auto& val = args[0].const_();
      if (val.has_str()) {
        tableName = val.str();
      }
    }
    return std::make_unique<TableInfoFuncInput>(std::move(tableName));
  };

  function->execFunc = [](const CallFuncInputBase& input,
                          neug::IStorageInterface& graph) {
    try {
      auto& tableInput = dynamic_cast<const TableInfoFuncInput&>(input);
      const std::string& tableName = tableInput.tableName;

      neug::execution::Context ctx;
      const auto& schema = graph.schema();

      // Try vertex first
      bool isVertex = schema.is_vertex_label_valid(tableName);
      bool isEdge = schema.is_edge_label_valid(tableName);

      if (!isVertex && !isEdge) {
        THROW_CATALOG_EXCEPTION("Table " + tableName +
                                " does not exist in catalog.");
      }

      neug::execution::ValueColumnBuilder<std::string> prop_name_builder;
      neug::execution::ValueColumnBuilder<std::string> prop_type_builder;
      neug::execution::ValueColumnBuilder<std::string> pk_builder;
      neug::execution::ValueColumnBuilder<std::string> src_builder;
      neug::execution::ValueColumnBuilder<std::string> dst_builder;

      if (isVertex) {
        auto labelId = schema.get_vertex_label_id(tableName);
        auto vSchema = schema.get_vertex_schema(labelId);
        if (!vSchema) {
          THROW_CATALOG_EXCEPTION("Vertex schema for " + tableName +
                                  " is null.");
        }

        // Collect primary key names for quick lookup
        std::unordered_set<std::string> pkNames;
        for (const auto& [dt, pkName, idx] : vSchema->primary_keys) {
          pkNames.insert(pkName);
        }

        prop_name_builder.reserve(vSchema->property_names.size());
        prop_type_builder.reserve(vSchema->property_names.size());
        pk_builder.reserve(vSchema->property_names.size());
        src_builder.reserve(vSchema->property_names.size());
        dst_builder.reserve(vSchema->property_names.size());

        for (size_t i = 0; i < vSchema->property_names.size(); ++i) {
          if (vSchema->vprop_soft_deleted.size() > i &&
              vSchema->vprop_soft_deleted[i]) {
            continue;
          }
          prop_name_builder.push_back_opt(vSchema->property_names[i]);
          prop_type_builder.push_back_opt(vSchema->property_types[i].ToString());
          pk_builder.push_back_opt(
              pkNames.count(vSchema->property_names[i]) ? "YES" : "NO");
          src_builder.push_back_opt("");
          dst_builder.push_back_opt("");
        }
      } else {
        // Edge: may have multiple triplets with the same edge_label_name
        const auto& edgeSchemas = schema.get_all_edge_schemas();
        size_t totalProps = 0;
        for (const auto& [_, es] : edgeSchemas) {
          if (es && es->edge_label_name == tableName &&
              !schema.is_edge_label_soft_deleted(es->src_label_name,
                                                  es->dst_label_name,
                                                  es->edge_label_name)) {
            totalProps += es->property_names.size();
          }
        }

        prop_name_builder.reserve(totalProps);
        prop_type_builder.reserve(totalProps);
        pk_builder.reserve(totalProps);
        src_builder.reserve(totalProps);
        dst_builder.reserve(totalProps);

        for (const auto& [_, es] : edgeSchemas) {
          if (!es || es->edge_label_name != tableName) continue;
          if (schema.is_edge_label_soft_deleted(es->src_label_name,
                                                 es->dst_label_name,
                                                 es->edge_label_name)) {
            continue;
          }

          for (size_t i = 0; i < es->property_names.size(); ++i) {
            if (es->eprop_soft_deleted.size() > i && es->eprop_soft_deleted[i]) {
              continue;
            }
            prop_name_builder.push_back_opt(es->property_names[i]);
            prop_type_builder.push_back_opt(es->properties[i].ToString());
            pk_builder.push_back_opt("NO");
            src_builder.push_back_opt(es->src_label_name);
            dst_builder.push_back_opt(es->dst_label_name);
          }
        }
      }

      ctx.set(0, prop_name_builder.finish());
      ctx.set(1, prop_type_builder.finish());
      ctx.set(2, pk_builder.finish());
      ctx.set(3, src_builder.finish());
      ctx.set(4, dst_builder.finish());
      ctx.tag_ids = {0, 1, 2, 3, 4};
      return ctx;
    } catch (const std::exception& e) {
      LOG(ERROR) << "TableInfo failed: " << e.what();
      throw;
    }
  };

  function_set functionSet;
  functionSet.push_back(std::move(function));
  return functionSet;
}

}  // namespace function
}  // namespace neug
