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
      child_executor_(std::move(child_executor)) {}

void InsertExecutor::Init() { 
    if (inited_) {
        index_ = 0;
        return;
    }

    child_executor_->Init();

    // Insert
    Tuple child_tuple{};
    RID child_rid{};
    TupleMeta tuple_meta_new{INVALID_TXN_ID, INVALID_TXN_ID, false};

    // Get table_info and index_infos
    auto table_oid = plan_->table_oid_;
    const auto table_info = exec_ctx_->GetCatalog()->GetTable(table_oid);
    const auto index_infos = exec_ctx_->GetCatalog()->GetTableIndexes(table_info->name_);
    int rt = 0;
    
    // Get the next tuple
    while (child_executor_->Next(&child_tuple, &child_rid)) {
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
    }

    // return the number of inserted rows
    std::vector<Value> values{};
    values.reserve(GetOutputSchema().GetColumnCount());
    values.push_back(ValueFactory::GetIntegerValue(rt));
    rt_.emplace_back(values, &GetOutputSchema());

    inited_ = true;
}

auto InsertExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool { 
    if (index_ >= rt_.size()) {
        return false;
    }
    *tuple = rt_[index_++];
    return true;
}

}  // namespace bustub
