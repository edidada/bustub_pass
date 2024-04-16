//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// transaction_manager.cpp
//
// Identification: src/concurrency/transaction_manager.cpp
//
// Copyright (c) 2015-2019, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "concurrency/transaction_manager.h"

#include <mutex>  // NOLINT
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

#include "catalog/catalog.h"
#include "common/macros.h"
#include "storage/table/table_heap.h"

namespace bustub {

void TransactionManager::Commit(Transaction *txn) {
  // Release all the locks.
  ReleaseLocks(txn);

  // Set state to COMMITTED
  txn->SetState(TransactionState::COMMITTED);

  // Erase txn from txn_map_
  std::unique_lock<std::shared_mutex> l(txn_map_mutex_);
  txn_map_.erase(txn->GetTransactionId());
}

void TransactionManager::Abort(Transaction *txn) {
  /* TODO: revert all the changes in write set */
  // revert table write records
  auto table_write_records = txn->GetWriteSet();
  while (!table_write_records->empty()) {
    auto &write_record = table_write_records->back();
    auto tuple_meta = write_record.table_heap_->GetTupleMeta(write_record.rid_);
    switch (write_record.wtype_) {
      case WType::INSERT:
        tuple_meta.is_deleted_ = true;
        write_record.table_heap_->UpdateTupleMeta(tuple_meta, write_record.rid_);
        break;
      case WType::DELETE:
        tuple_meta.is_deleted_ = false;
        write_record.table_heap_->UpdateTupleMeta(tuple_meta, write_record.rid_);
        break;
      default:
        break;
    }
    table_write_records->pop_back();
  }
  // revert index write records
  auto index_write_records = txn->GetIndexWriteSet();
  while (!index_write_records->empty()) {
    auto &write_record = index_write_records->back();
    switch (write_record.wtype_) {
      case WType::INSERT:
        write_record.catalog_->GetIndex(write_record.index_oid_)->index_->DeleteEntry(write_record.tuple_, write_record.rid_, txn);
        break;
      case WType::DELETE:
        write_record.catalog_->GetIndex(write_record.index_oid_)->index_->InsertEntry(write_record.tuple_, write_record.rid_, txn);
        break;
      case WType::UPDATE:
        break;
    }
    index_write_records->pop_back();
  }

  // Release all the locks.
  ReleaseLocks(txn);

  // Set state to COMMITTED
  txn->SetState(TransactionState::ABORTED);

  // Erase txn from txn_map_
  std::unique_lock<std::shared_mutex> l(txn_map_mutex_);
  txn_map_.erase(txn->GetTransactionId());
}

void TransactionManager::BlockAllTransactions() { UNIMPLEMENTED("block is not supported now!"); }

void TransactionManager::ResumeTransactions() { UNIMPLEMENTED("resume is not supported now!"); }

}  // namespace bustub
