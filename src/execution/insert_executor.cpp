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
    try {
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

        // try lock table
        if (!exec_ctx_->GetLockManager()->LockTable(exec_ctx_->GetTransaction(), LockManager::LockMode::INTENTION_EXCLUSIVE, table_oid)) {
            throw ExecutionException("Lock Table Failed");
        }

        // Get the next tuple
        while (child_executor_->Next(&child_tuple, &child_rid)) {
            // Insert into table
            auto tuple_rid_new = table_info->table_->InsertTuple(tuple_meta_new, child_tuple, exec_ctx_->GetLockManager(), exec_ctx_->GetTransaction(), table_oid);    
            if (!tuple_rid_new) {
                break;
            }
            // Append table write records
            auto record = TableWriteRecord(table_oid, tuple_rid_new.value(), table_info->table_.get());
            record.wtype_ = WType::INSERT;
            exec_ctx_->GetTransaction()->AppendTableWriteRecord(record);

            // Insert into indexes
            for (auto &index_info : index_infos) {
                auto key = child_tuple.KeyFromTuple(table_info->schema_, index_info->key_schema_, index_info->index_->GetKeyAttrs());
                index_info->index_->InsertEntry(key, *tuple_rid_new, exec_ctx_->GetTransaction());
                // Append index write records
                exec_ctx_->GetTransaction()->AppendIndexWriteRecord(IndexWriteRecord(
                    *tuple_rid_new, table_oid, WType::INSERT, key, index_info->index_oid_, exec_ctx_->GetCatalog()));
            }
            ++rt;
        }

        // return the number of inserted rows
        std::vector<Value> values{};
        values.reserve(GetOutputSchema().GetColumnCount());
        values.push_back(ValueFactory::GetIntegerValue(rt));
        rt_.emplace_back(values, &GetOutputSchema());

        inited_ = true;

    } catch (TransactionAbortException e) {
        throw ExecutionException("Insert Init failed: " + e.GetInfo());
    }
}

auto InsertExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool { 
    if (index_ >= rt_.size()) {
        return false;
    }
    *tuple = rt_[index_++];
    return true;
}

}  // namespace bustub
