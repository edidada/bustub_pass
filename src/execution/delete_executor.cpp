//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// delete_executor.cpp
//
// Identification: src/execution/delete_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>

#include "execution/executors/delete_executor.h"

namespace bustub {

DeleteExecutor::DeleteExecutor(ExecutorContext *exec_ctx, const DeletePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    :   AbstractExecutor(exec_ctx), plan_(plan), 
        child_executor_(std::move(child_executor)),
        index_(0) {}

void DeleteExecutor::Init() { 
    if (inited_) {
        index_ = 0;
        return;
    }

    child_executor_->Init();

    // Delete
    Tuple child_tuple{};
    RID child_rid{};
    TupleMeta tuple_meta_delete{INVALID_TXN_ID, INVALID_TXN_ID, true};

    // Delete from table
    auto table_oid = plan_->table_oid_;
    const auto table_info = exec_ctx_->GetCatalog()->GetTable(table_oid);
    const auto index_infos = exec_ctx_->GetCatalog()->GetTableIndexes(table_info->name_);
    int rt = 0;
    
    // Get the next tuple
    while (child_executor_->Next(&child_tuple, &child_rid)) {
        // Delete from table
        auto tuple_rid_delete = child_tuple.GetRid();
        table_info->table_->UpdateTupleMeta(tuple_meta_delete, tuple_rid_delete);    
        // Append table write record
        auto record = TableWriteRecord(table_oid, tuple_rid_delete, table_info->table_.get());
        record.wtype_ = WType::DELETE;
        exec_ctx_->GetTransaction()->AppendTableWriteRecord(record);

        // Delete from indexes
        for (auto &index_info : index_infos) {
            auto key = child_tuple.KeyFromTuple(table_info->schema_, index_info->key_schema_, index_info->index_->GetKeyAttrs());
            index_info->index_->DeleteEntry(key, tuple_rid_delete, exec_ctx_->GetTransaction());
            // Append index write record
            exec_ctx_->GetTransaction()->AppendIndexWriteRecord(IndexWriteRecord(
                tuple_rid_delete, table_info->oid_, WType::DELETE, key, index_info->index_oid_, exec_ctx_->GetCatalog()));
        }
        ++rt;
    }
    std::vector<Value> values{};
    values.reserve(GetOutputSchema().GetColumnCount());
    values.push_back(ValueFactory::GetIntegerValue(rt));
    rt_.emplace_back(values, &GetOutputSchema());

    inited_ = true;
}

auto DeleteExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool { 
    if (index_ >= rt_.size()) {
        return false;
    }

    *tuple = rt_[index_++];
    return true;    
}

}  // namespace bustub
