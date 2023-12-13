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
        returned_(false) {}

void DeleteExecutor::Init() { 
    child_executor_->Init();
}

auto DeleteExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool { 
    if (returned_) {
        return false;
    }

    Tuple child_tuple{};
    TupleMeta tuple_meta_delete{INVALID_TXN_ID, INVALID_TXN_ID, true};

    // Delete from table
    auto table_oid = plan_->GetTableOid();
    const auto table_info = exec_ctx_->GetCatalog()->GetTable(table_oid);
    const auto index_infos = exec_ctx_->GetCatalog()->GetTableIndexes(table_info->name_);
    int rt = 0;
    
    // Get the next tuple
    auto status = child_executor_->Next(&child_tuple, nullptr);
    while (status) {
        // Delete from table
        auto tuple_rid_delete = child_tuple.GetRid();
        table_info->table_->UpdateTupleMeta(tuple_meta_delete, tuple_rid_delete);    
        // Delete from indexes
        for (auto &index_info : index_infos) {
            index_info->index_->DeleteEntry(child_tuple.KeyFromTuple(table_info->schema_, index_info->key_schema_, index_info->index_->GetKeyAttrs()), tuple_rid_delete, exec_ctx_->GetTransaction());
        }
        ++rt;
        status = child_executor_->Next(&child_tuple, nullptr);
    }
    std::vector<Value> values{};
    values.reserve(GetOutputSchema().GetColumnCount());
    values.push_back(ValueFactory::GetIntegerValue(rt));
    *tuple = Tuple{values, &GetOutputSchema()};
    returned_ = true;
    return true;    
}

}  // namespace bustub
