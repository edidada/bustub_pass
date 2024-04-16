//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// seq_scan_executor.cpp
//
// Identification: src/execution/seq_scan_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/seq_scan_executor.h"
#include <cstddef>
#include <memory>
#include <vector>
#include "storage/table/table_iterator.h"
#include "type/type.h"

namespace bustub {

SeqScanExecutor::SeqScanExecutor(ExecutorContext *exec_ctx, const SeqScanPlanNode *plan) 
    : AbstractExecutor(exec_ctx), 
      plan_(plan),
      table_iter_(nullptr) {}

void SeqScanExecutor::Init() {
  // try take table lock
  try {
    auto lock_manager = exec_ctx_->GetLockManager();
    bool success = true;
    // If current seqscan is for delete, try take IX table lock 
    if (exec_ctx_->IsDelete()) {
      success = lock_manager->LockTable(exec_ctx_->GetTransaction(), LockManager::LockMode::INTENTION_EXCLUSIVE, plan_->table_oid_);
    // take IS table lock if possible
    } else if (exec_ctx_->GetTransaction()->GetIsolationLevel() != IsolationLevel::READ_UNCOMMITTED && 
              !exec_ctx_->GetTransaction()->IsTableIntentionExclusiveLocked(plan_->table_oid_)) {
      success = lock_manager->LockTable(exec_ctx_->GetTransaction(), LockManager::LockMode::INTENTION_SHARED, plan_->table_oid_);
    }
    if (!success) {
      throw ExecutionException("Take table lock failed");
    }
  } catch (TransactionAbortException e){
    throw ExecutionException("SeqScan Init failed: " + e.GetInfo());
  }

  // init the table iterator
  table_iter_ = std::make_unique<TableIterator>(exec_ctx_->GetCatalog()->GetTable(plan_->table_oid_)->table_->MakeEagerIterator());
}

auto SeqScanExecutor::Next(Tuple *tuple, RID *rid) -> bool { 
  try {
    auto lock_manager = exec_ctx_->GetLockManager();
    while (!table_iter_->IsEnd()) {
      *rid = table_iter_->GetRID();

      // try to take row lock
      auto success = true;
      if (exec_ctx_->IsDelete()) {
        success = lock_manager->LockRow(exec_ctx_->GetTransaction(), LockManager::LockMode::INTENTION_EXCLUSIVE, plan_->table_oid_, *rid);
      } else if (exec_ctx_->GetTransaction()->GetIsolationLevel() != IsolationLevel::READ_UNCOMMITTED && 
                !exec_ctx_->GetTransaction()->IsRowExclusiveLocked(plan_->table_oid_, *rid)) {
        success = lock_manager->LockRow(exec_ctx_->GetTransaction(), LockManager::LockMode::INTENTION_SHARED, plan_->table_oid_, *rid);
      }
      if (!success) {
        throw ExecutionException("Lock row failed");
      }

      auto [tuple_meta, tmp_tuple] = table_iter_->GetTuple();
      ++(*table_iter_);

      // check if the tuple is valid
      if (tuple_meta.is_deleted_) {
        if (exec_ctx_->IsDelete() || exec_ctx_->GetTransaction()->GetIsolationLevel() != IsolationLevel::READ_UNCOMMITTED) {
          success = lock_manager->UnlockRow(exec_ctx_->GetTransaction(), plan_->table_oid_, *rid);
        }
        if (!success) {
          throw ExecutionException("Unlock row failed");
        }
        continue;
      } 

      *tuple = tmp_tuple;
      if (!exec_ctx_->IsDelete() && exec_ctx_->GetTransaction()->GetIsolationLevel() == IsolationLevel::READ_COMMITTED) {
        success = lock_manager->UnlockRow(exec_ctx_->GetTransaction(), plan_->table_oid_, *rid);
      }
      if (!success) {
        throw ExecutionException("Unlock row failed");
      }
      return true;
    }
    return false;

  } catch (TransactionAbortException e){
    throw ExecutionException("SeqScan Next failed: " + e.GetInfo());
  }
}

}  // namespace bustub
