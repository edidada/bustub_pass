//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// update_executor.cpp
//
// Identification: src/execution/update_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#include <memory>

#include "execution/executors/update_executor.h"
#include "storage/table/tuple.h"

namespace bustub {

UpdateExecutor::UpdateExecutor(ExecutorContext *exec_ctx, const UpdatePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), 
      plan_(plan), 
      child_executor_(std::move(child_executor)),
      returned_(false) {
  // As of Fall 2022, you DON'T need to implement update executor to have perfect score in project 3 / project 4.
}

void UpdateExecutor::Init() { 
  child_executor_->Init();
}

auto UpdateExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool { 
  if (returned_) {
    return false;
  }

  auto table_oid = plan_->GetTableOid();
  const auto table_info = exec_ctx_->GetCatalog()->GetTable(table_oid);
  const auto index_infos = exec_ctx_->GetCatalog()->GetTableIndexes(table_info->name_);

  Tuple child_tuple{};
  TupleMeta tuple_meta_delete{INVALID_TXN_ID, INVALID_TXN_ID, true};
  TupleMeta tuple_meta_new{INVALID_TXN_ID, INVALID_TXN_ID, false};

  int rt = 0;

  // Get the next tuple
  auto status = child_executor_->Next(&child_tuple, nullptr);
  while (status) {
    // Remove from table(update table meta)
    auto tuple_rid_delete = child_tuple.GetRid();
    table_info->table_->UpdateTupleMeta(tuple_meta_delete, tuple_rid_delete);
    // Remove from indexs
    for (auto &index_info : index_infos) {
      auto key_delete = child_tuple.KeyFromTuple(table_info->schema_, index_info->key_schema_, index_info->index_->GetKeyAttrs());
      index_info->index_->DeleteEntry(key_delete, tuple_rid_delete, exec_ctx_->GetTransaction());
    }
    // Insert into table
    std::vector<Value> values{};
    values.reserve(GetOutputSchema().GetColumnCount());
    for (auto &expr : plan_->target_expressions_) {
      values.push_back(expr->Evaluate(&child_tuple, child_executor_->GetOutputSchema()));
    }
    Tuple new_tuple = Tuple{values, &table_info->schema_};
    auto tuple_rid_new = table_info->table_->InsertTuple(tuple_meta_new, new_tuple);
    if (!tuple_rid_new) {
      break;
    }
    // Insert into indexs
    for (auto &index_info : index_infos) {
      auto key_new = new_tuple.KeyFromTuple(table_info->schema_, index_info->key_schema_, index_info->index_->GetKeyAttrs());
      index_info->index_->InsertEntry(key_new, *tuple_rid_new, exec_ctx_->GetTransaction());
    }
    ++rt;
    status = child_executor_->Next(&child_tuple, nullptr);
  }

  // return the number of inserted rows
  std::vector<Value> values{};
  values.reserve(GetOutputSchema().GetColumnCount());
  values.push_back(ValueFactory::GetIntegerValue(rt));
  *tuple = Tuple{values, &GetOutputSchema()};
  returned_ = true;
  return true;
}

}  // namespace bustub
