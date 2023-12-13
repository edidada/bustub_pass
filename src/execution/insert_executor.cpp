//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// insert_executor.cpp
//
// Identification: src/execution/insert_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <memory>

#include "common/config.h"
#include "execution/executors/insert_executor.h"
#include "storage/table/tuple.h"
#include "type/value_factory.h"

namespace bustub {

InsertExecutor::InsertExecutor(ExecutorContext *exec_ctx, const InsertPlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), 
      child_executor_(std::move(child_executor)), returned_(false) {}

void InsertExecutor::Init() { 
    child_executor_->Init();
}

auto InsertExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool { 
    if (returned_) {
        return false;
    }

    Tuple child_tuple{};
    TupleMeta tuple_meta_new{INVALID_TXN_ID, INVALID_TXN_ID, false};

    // Get table_info and index_infos
    auto table_oid = plan_->GetTableOid();
    const auto table_info = exec_ctx_->GetCatalog()->GetTable(table_oid);
    const auto index_infos = exec_ctx_->GetCatalog()->GetTableIndexes(table_info->name_);
    int rt = 0;
    
    // Get the next tuple
    auto status = child_executor_->Next(&child_tuple, nullptr);
    while (status) {
        // Insert into table
        auto tuple_rid_new = table_info->table_->InsertTuple(tuple_meta_new, child_tuple, exec_ctx_->GetLockManager(), exec_ctx_->GetTransaction(), table_oid);    
        if (!tuple_rid_new) {
            break;
        }
        // Insert into indexes
        for (auto &index_info : index_infos) {
            index_info->index_->InsertEntry(child_tuple.KeyFromTuple(table_info->schema_, index_info->key_schema_, index_info->index_->GetKeyAttrs()), *tuple_rid_new, exec_ctx_->GetTransaction());
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
