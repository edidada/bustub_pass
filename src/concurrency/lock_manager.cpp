//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// lock_manager.cpp
//
// Identification: src/concurrency/lock_manager.cpp
//
// Copyright (c) 2015-2019, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "concurrency/lock_manager.h"
#include <iterator>
#include <memory>
#include <mutex>
#include <unordered_set>

#include "common/config.h"
#include "common/macros.h"
#include "concurrency/transaction.h"
#include "concurrency/transaction_manager.h"

namespace bustub {

auto LockManager::LockTable(Transaction *txn, LockMode lock_mode, const table_oid_t &oid) -> bool { 

  // 2PL check if the txn can take the lock
  if (!CanTxnTakeLock(txn, lock_mode)) {
    return false;
  }

  // try to get the lock_request_queue on the table
  std::unique_lock<std::mutex> table_lock_map_latch(table_lock_map_latch_);
  auto lock_request_queue_ptr = GetOrCreateLockRequestQueue(oid);
  std::unique_lock<std::mutex> lock_request_queue_latch(lock_request_queue_ptr->latch_);  
  table_lock_map_latch.unlock();
  
  // try to get lock on the table
  auto new_lock_request = std::make_shared<LockRequest>(txn->GetTransactionId(), lock_mode, oid);
  auto request_queue_iter = FindLockRequest(lock_request_queue_ptr.get(), txn->GetTransactionId(), oid);
  // if new_lock_request is not in the queue
  if (request_queue_iter == lock_request_queue_ptr->request_queue_.end()) {
    lock_request_queue_ptr->request_queue_.push_back(new_lock_request);
  } else {
    auto old_lock_request = *request_queue_iter;
    // if the lock mode is already hold by the txn, return true
    if (old_lock_request->lock_mode_ == lock_mode) {
      return true;
    }
    // if the lock mode is not hold by the txn, upgrade the lock mode 
    if (!CanLockUpgrade(txn, lock_request_queue_ptr.get(), old_lock_request->lock_mode_, lock_mode)) {
      return false;
    }
    RemoveTableLockInTxn(txn, old_lock_request->lock_mode_, old_lock_request->oid_);
    request_queue_iter = lock_request_queue_ptr->request_queue_.erase(request_queue_iter);
    while (request_queue_iter != lock_request_queue_ptr->request_queue_.end() && 
           (*request_queue_iter)->granted_) { request_queue_iter++; }
    lock_request_queue_ptr->request_queue_.insert(request_queue_iter, new_lock_request);
    lock_request_queue_ptr->upgrading_ = txn->GetTransactionId();
  }

  GrantNewLocksIfPossible(lock_request_queue_ptr.get());
  // wait until the lock is granted
  while (!new_lock_request->granted_) {
    lock_request_queue_ptr->cv_.wait(lock_request_queue_latch);
  }

  // if the lock is granted, add the lock to txn
  AddTableLockInTxn(txn, lock_mode, oid);
  if (lock_request_queue_ptr->upgrading_ == txn->GetTransactionId()) {
    lock_request_queue_ptr->upgrading_ = INVALID_TXN_ID;
  }
  return true;
}

void LockManager::GrantNewLocksIfPossible(LockRequestQueue *lock_request_queue) {
  if (lock_request_queue->request_queue_.empty()) { return; }
  // grant first lock request in the queue
  auto request_queue_iter = lock_request_queue->request_queue_.begin();
  (*request_queue_iter)->granted_ = true;
  request_queue_iter++;
  // grant the other lock requests in the queue if possible
  for (; request_queue_iter != lock_request_queue->request_queue_.end(); request_queue_iter++) {
    bool can_grant = true;
    for (auto p = lock_request_queue->request_queue_.begin(); p != request_queue_iter; p++) {
      if (!(*p)->granted_ || !AreLocksCompatible((*p)->lock_mode_, (*request_queue_iter)->lock_mode_)) {
        can_grant = false;
        break;
      }
    }
    if (!can_grant) {
      break;
    }
    (*request_queue_iter)->granted_ = true;
  }
}

auto LockManager::UnlockTable(Transaction *txn, const table_oid_t &oid) -> bool { 
  // check if the txn holds the table lock
  LockMode lock_mode;
  if (!GetTableLockModeInTxn(txn, oid, &lock_mode)) {
    txn->SetState(TransactionState::ABORTED);
    throw TransactionAbortException(txn->GetTransactionId(), AbortReason::ATTEMPTED_UNLOCK_BUT_NO_LOCK_HELD);
    return false;
  }
  // check if the txn holds the row lock on the table
  if (IsRowLockedInTxn(txn, oid)) {
    txn->SetState(TransactionState::ABORTED);
    throw TransactionAbortException(txn->GetTransactionId(), AbortReason::TABLE_UNLOCKED_BEFORE_UNLOCKING_ROWS);
    return false;
  }

  // try to get the lock_request_queue on the table
  std::unique_lock<std::mutex> table_lock_map_latch(table_lock_map_latch_);
  auto iter = table_lock_map_.find(oid);
  if (iter == table_lock_map_.end()) {
    return false;
  }
  std::shared_ptr<LockRequestQueue> lock_request_queue_ptr = iter->second;
  std::unique_lock<std::mutex> lock_request_queue_latch(lock_request_queue_ptr->latch_);
  table_lock_map_latch.unlock();

  // try to unlock the table
  auto request_queue_iter = FindLockRequest(lock_request_queue_ptr.get(), txn->GetTransactionId(), oid);
  BUSTUB_ASSERT(request_queue_iter != lock_request_queue_ptr->request_queue_.end(), "unlock invalid txn");
  BUSTUB_ASSERT(lock_mode == (*request_queue_iter)->lock_mode_, "unlock invalid lock mode");
  RemoveTableLockInTxn(txn, lock_mode, oid);
  lock_request_queue_ptr->request_queue_.erase(request_queue_iter);
  // 2PL actions
  if (txn->GetIsolationLevel() == IsolationLevel::REPEATABLE_READ) {
    if (lock_mode == LockMode::SHARED || lock_mode == LockMode::EXCLUSIVE) {
      txn->SetState(TransactionState::SHRINKING);
    }
  } else {
    if (lock_mode == LockMode::EXCLUSIVE) {
      txn->SetState(TransactionState::SHRINKING);
    }
  }
  GrantNewLocksIfPossible(lock_request_queue_ptr.get());
  lock_request_queue_latch.unlock();
  lock_request_queue_ptr->cv_.notify_all();
  return true;
}

auto LockManager::LockRow(Transaction *txn, LockMode lock_mode, const table_oid_t &oid, const RID &rid) -> bool {
  // check if lock_mode is valid
  if (lock_mode == LockMode::INTENTION_EXCLUSIVE ||
      lock_mode == LockMode::SHARED_INTENTION_EXCLUSIVE ||
      lock_mode == LockMode::INTENTION_SHARED) {
    txn->SetState(TransactionState::ABORTED);
    throw TransactionAbortException(txn->GetTransactionId(), AbortReason::ATTEMPTED_INTENTION_LOCK_ON_ROW);
    return false;
  }
  // 2PL check if the txn can take the lock
  if (!CanTxnTakeLock(txn, lock_mode)) {
    return false;
  }
  // check if the txn holds the table lock
  LockMode table_lock_mode;
  if (!GetTableLockModeInTxn(txn, oid, &table_lock_mode) || 
      (lock_mode == LockMode::SHARED && table_lock_mode != LockMode::INTENTION_SHARED && table_lock_mode != LockMode::SHARED) || 
      (lock_mode == LockMode::EXCLUSIVE && table_lock_mode != LockMode::INTENTION_EXCLUSIVE && table_lock_mode != LockMode::SHARED_INTENTION_EXCLUSIVE && table_lock_mode != LockMode::EXCLUSIVE)) {
    txn->SetState(TransactionState::ABORTED);
    throw TransactionAbortException(txn->GetTransactionId(), AbortReason::TABLE_LOCK_NOT_PRESENT);
    return false;
  }

  // try to get the lock_request_queue on the row
  std::unique_lock<std::mutex> row_lock_map_latch(row_lock_map_latch_);
  std::shared_ptr<LockRequestQueue> lock_request_queue_ptr = GetOrCreateLockRequestQueue(rid);
  std::unique_lock<std::mutex> lock_request_queue_latch(lock_request_queue_ptr->latch_);  
  row_lock_map_latch.unlock();

  // try to get lock on the row
  auto new_lock_request = std::make_shared<LockRequest>(txn->GetTransactionId(), lock_mode, oid, rid);
  // find lock_request in the queue
  auto request_queue_iter = FindLockRequest(lock_request_queue_ptr.get(), txn->GetTransactionId(), rid);
  // if new_lock_request is not in the queue
  if (request_queue_iter == lock_request_queue_ptr->request_queue_.end()) {
    lock_request_queue_ptr->request_queue_.push_back(new_lock_request);
  } else {
  // if new_lock_request is already in the queue
    auto old_lock_request = *request_queue_iter;
    // if the lock mode is already hold by the txn, return true
    if (old_lock_request->lock_mode_ == lock_mode) {
      return true;
    }
    // if the lock mode is not hold by the txn, upgrade the lock mode 
    if (!CanLockUpgrade(txn, lock_request_queue_ptr.get(), old_lock_request->lock_mode_, lock_mode)) {
      return false;
    }
    RemoveRowLockInTxn(txn, old_lock_request->lock_mode_, old_lock_request->oid_, old_lock_request->rid_);
    request_queue_iter = lock_request_queue_ptr->request_queue_.erase(request_queue_iter);
    while (request_queue_iter != lock_request_queue_ptr->request_queue_.end() && 
           (*request_queue_iter)->granted_) { request_queue_iter++; }
    lock_request_queue_ptr->request_queue_.insert(request_queue_iter, new_lock_request);
    lock_request_queue_ptr->upgrading_ = txn->GetTransactionId();
  }

  GrantNewLocksIfPossible(lock_request_queue_ptr.get());
  // wait until the lock is granted
  while (!new_lock_request->granted_) {
    lock_request_queue_ptr->cv_.wait(lock_request_queue_latch);
  }

  // if the lock is granted, add the lock to txn
  AddRowLockInTxn(txn, lock_mode, oid, rid);
  if (lock_request_queue_ptr->upgrading_ == txn->GetTransactionId()) {
    lock_request_queue_ptr->upgrading_ = INVALID_TXN_ID;
  }
  return true;
}

auto LockManager::UnlockRow(Transaction *txn, const table_oid_t &oid, const RID &rid, bool force) -> bool {
  // check if the txn holds the row lock
  LockMode lock_mode;
  if (!GetRowLockModeInTxn(txn, oid, rid, &lock_mode)) {
    txn->SetState(TransactionState::ABORTED);
    throw TransactionAbortException(txn->GetTransactionId(), AbortReason::ATTEMPTED_UNLOCK_BUT_NO_LOCK_HELD);
    return false;
  }

  // try to get the lock_request_queue on the row
  std::unique_lock<std::mutex> lock_request_queue_latch(row_lock_map_latch_);
  auto iter = row_lock_map_.find(rid);
  if (iter == row_lock_map_.end()) {
    return false;
  }
  std::shared_ptr<LockRequestQueue> lock_request_queue_ptr = iter->second;
  std::unique_lock<std::mutex> lock(lock_request_queue_ptr->latch_);
  lock_request_queue_latch.unlock();

  // try to unlock the row
  auto request_queue_iter = FindLockRequest(lock_request_queue_ptr.get(), txn->GetTransactionId(), rid);
  BUSTUB_ASSERT(lock_mode == (*request_queue_iter)->lock_mode_, "invalid lock mode");
  RemoveRowLockInTxn(txn, lock_mode, oid, rid);
  lock_request_queue_ptr->request_queue_.erase(request_queue_iter);
  // 2PL actions
  if (!force) {
    if (txn->GetIsolationLevel() == IsolationLevel::REPEATABLE_READ) {
      if (lock_mode == LockMode::SHARED || lock_mode == LockMode::EXCLUSIVE) {
        txn->SetState(TransactionState::SHRINKING);
      }
    } else {
      if (lock_mode == LockMode::EXCLUSIVE) {
        txn->SetState(TransactionState::SHRINKING);
      }
    }
  }
  GrantNewLocksIfPossible(lock_request_queue_ptr.get());
  lock_request_queue_latch.unlock();
  lock_request_queue_ptr->cv_.notify_all();
  return true;
}

void LockManager::UnlockAll() {
  // You probably want to unlock all table and txn locks here.
  std::unique_lock<std::mutex> table_lock_map_latch(table_lock_map_latch_);
  std::vector<Transaction*> release_txns;
  for (auto iter = table_lock_map_.begin(); iter != table_lock_map_.end(); iter++) {
    auto lock_request_queue_ptr = iter->second;
    {
      std::unique_lock<std::mutex> lock_request_queue_latch(lock_request_queue_ptr->latch_);
      for (auto request_queue_iter = lock_request_queue_ptr->request_queue_.begin(); request_queue_iter != lock_request_queue_ptr->request_queue_.end(); request_queue_iter++) {
        (*request_queue_iter)->granted_ = true;
      }
    }
  }
}

void LockManager::AddEdge(txn_id_t t1, txn_id_t t2) {}

void LockManager::RemoveEdge(txn_id_t t1, txn_id_t t2) {}

auto LockManager::HasCycle(txn_id_t *txn_id) -> bool { return false; }

auto LockManager::GetEdgeList() -> std::vector<std::pair<txn_id_t, txn_id_t>> {
  std::vector<std::pair<txn_id_t, txn_id_t>> edges(0);
  return edges;
}

void LockManager::RunCycleDetection() {
  while (enable_cycle_detection_) {
    std::this_thread::sleep_for(cycle_detection_interval);
    {  // TODO(students): detect deadlock
    }
  }
}

auto LockManager::AreLocksCompatible(LockMode l1, LockMode l2) -> bool { 
  switch (l1) {
  case LockMode::INTENTION_SHARED:
    return l2 != LockMode::EXCLUSIVE;
  case LockMode::INTENTION_EXCLUSIVE:
    return l2 == LockMode::INTENTION_SHARED || l2 == LockMode::INTENTION_EXCLUSIVE;
  case LockMode::SHARED:
    return l2 == LockMode::INTENTION_SHARED || l2 == LockMode::SHARED;
  case LockMode::SHARED_INTENTION_EXCLUSIVE:
    return l2 == LockMode::INTENTION_SHARED;
  case LockMode::EXCLUSIVE:
  default:
    return false;
  }
}

auto LockManager::CanTxnTakeLock(Transaction *txn, LockMode lock_mode) -> bool {
  auto isolation_level = txn->GetIsolationLevel();
  auto txn_state = txn->GetState();

  if (txn_state == TransactionState::ABORTED) {
    return false;
  }

  if (txn_state == TransactionState::SHRINKING) {
    if (isolation_level != IsolationLevel::READ_COMMITTED || 
        (lock_mode != LockMode::SHARED && lock_mode != LockMode::INTENTION_SHARED)) {
      txn->SetState(TransactionState::ABORTED);
      throw TransactionAbortException(txn->GetTransactionId(), AbortReason::LOCK_ON_SHRINKING);
      return false;
    }
  }

  if (txn_state == TransactionState::GROWING) {
    if (isolation_level == IsolationLevel::READ_UNCOMMITTED) {
      if (lock_mode != LockMode::EXCLUSIVE && lock_mode != LockMode::INTENTION_EXCLUSIVE) {
        txn->SetState(TransactionState::ABORTED);
        throw TransactionAbortException(txn->GetTransactionId(), AbortReason::LOCK_SHARED_ON_READ_UNCOMMITTED);
        return false;
      }
    }
  }

  return true;
}

auto LockManager::CanLockUpgrade(Transaction *txn, LockRequestQueue *lock_request_queue, LockMode curr_lock_mode, LockMode requested_lock_mode) -> bool {
  if (lock_request_queue->upgrading_ != INVALID_TXN_ID) {
    txn->SetState(TransactionState::ABORTED);
    throw TransactionAbortException(txn->GetTransactionId(), AbortReason::UPGRADE_CONFLICT);
    return false;
  }
  if (curr_lock_mode == LockMode::INTENTION_SHARED) {
    if (requested_lock_mode != LockMode::INTENTION_SHARED) {
      return true;
    }
  } else if (curr_lock_mode == LockMode::SHARED || curr_lock_mode == LockMode::INTENTION_EXCLUSIVE) {
    if (requested_lock_mode == LockMode::EXCLUSIVE || requested_lock_mode == LockMode::SHARED_INTENTION_EXCLUSIVE) {
      return true;
    }
  } else if (curr_lock_mode == LockMode::SHARED_INTENTION_EXCLUSIVE) {
    if (requested_lock_mode == LockMode::EXCLUSIVE) {
      return true;
    }
  }  
  txn->SetState(TransactionState::ABORTED);
  throw TransactionAbortException(txn->GetTransactionId(), AbortReason::INCOMPATIBLE_UPGRADE);
  return false;
}

void LockManager::AddTableLockInTxn(Transaction *txn, LockMode lock_mode, const table_oid_t &oid) {
  switch (lock_mode) {
  case LockMode::INTENTION_SHARED:
    txn->GetIntentionSharedTableLockSet()->insert(oid);
    break;
  case LockMode::SHARED:
    txn->GetSharedTableLockSet()->insert(oid);
    break;
  case LockMode::INTENTION_EXCLUSIVE:
    txn->GetIntentionExclusiveTableLockSet()->insert(oid);
    break;
  case LockMode::SHARED_INTENTION_EXCLUSIVE:
    txn->GetSharedIntentionExclusiveTableLockSet()->insert(oid);
    break;
  case LockMode::EXCLUSIVE:
    txn->GetExclusiveTableLockSet()->insert(oid);
    break;
  default:
    BUSTUB_ASSERT(false, "invalid lock mode");
  }
}

void LockManager::RemoveTableLockInTxn(Transaction *txn, LockMode lock_mode, const table_oid_t &oid) {
  switch (lock_mode) {
  case LockMode::INTENTION_SHARED:
    txn->GetIntentionSharedTableLockSet()->erase(oid);
    break;
  case LockMode::SHARED:
    txn->GetSharedTableLockSet()->erase(oid);
    break;
  case LockMode::INTENTION_EXCLUSIVE:
    txn->GetIntentionExclusiveTableLockSet()->erase(oid);
    break;
  case LockMode::SHARED_INTENTION_EXCLUSIVE:
    txn->GetSharedIntentionExclusiveTableLockSet()->erase(oid);
    break;
  case LockMode::EXCLUSIVE:
    txn->GetExclusiveTableLockSet()->erase(oid);
    break;
  default:
    BUSTUB_ASSERT(false, "invalid lock mode");
  }
}

void LockManager::AddRowLockInTxn(Transaction *txn, LockMode lock_mode, const table_oid_t &oid, const RID &rid) {
  switch (lock_mode) {
  case LockMode::SHARED:
    if (txn->GetSharedRowLockSet()->find(oid) == txn->GetSharedRowLockSet()->end()) {
      txn->GetSharedRowLockSet()->insert({oid, std::unordered_set<RID>()});
    }
    txn->GetSharedRowLockSet()->at(oid).insert(rid);
    break;
  case LockMode::EXCLUSIVE:
    if (txn->GetExclusiveRowLockSet()->find(oid) == txn->GetExclusiveRowLockSet()->end()) {
      txn->GetExclusiveRowLockSet()->insert({oid, std::unordered_set<RID>()});
    }
    txn->GetExclusiveRowLockSet()->at(oid).insert(rid);
    break;
  default:
    BUSTUB_ASSERT(false, "invalid lock mode");
  }
}

void LockManager::RemoveRowLockInTxn(Transaction *txn, LockMode lock_mode, const table_oid_t &oid, const RID &rid) {
  switch (lock_mode) {
  case LockMode::SHARED:
    if (!txn->GetSharedRowLockSet()->at(oid).empty()) {
      txn->GetSharedRowLockSet()->erase(oid);
    }
    break;
  case LockMode::EXCLUSIVE:
    if (!txn->GetExclusiveRowLockSet()->at(oid).empty()) {
      txn->GetExclusiveRowLockSet()->erase(oid);
    }
    break;
  default:
    BUSTUB_ASSERT(false, "invalid lock mode");
  }
}

auto LockManager::GetTableLockModeInTxn(Transaction *txn, const table_oid_t &oid, LockMode* lock_mode) -> bool {
  bool rt = false;
  if (txn->GetIntentionSharedTableLockSet()->find(oid) != txn->GetIntentionSharedTableLockSet()->end()) {
    *lock_mode = LockMode::INTENTION_SHARED;
    rt = true;
  } else if (txn->GetSharedTableLockSet()->find(oid) != txn->GetSharedTableLockSet()->end()) {
    *lock_mode = LockMode::SHARED;
    rt = true;
  } else if (txn->GetIntentionExclusiveTableLockSet()->find(oid) != txn->GetIntentionExclusiveTableLockSet()->end()) {
    *lock_mode = LockMode::INTENTION_EXCLUSIVE;
    rt = true;
  } else if (txn->GetSharedIntentionExclusiveTableLockSet()->find(oid) != txn->GetSharedIntentionExclusiveTableLockSet()->end()) {
    *lock_mode = LockMode::SHARED_INTENTION_EXCLUSIVE;
    rt = true;
  } else if (txn->GetExclusiveTableLockSet()->find(oid) != txn->GetExclusiveTableLockSet()->end()) {
    *lock_mode = LockMode::EXCLUSIVE;
    rt = true;
  }
  return rt;
}

auto LockManager::IsRowLockedInTxn(Transaction *txn, const table_oid_t &oid) -> bool {
  if (txn->GetSharedRowLockSet()->find(oid) != txn->GetSharedRowLockSet()->end() &&
      !txn->GetSharedRowLockSet()->at(oid).empty()) {
    return true;
  }
  if (txn->GetExclusiveRowLockSet()->find(oid) != txn->GetExclusiveRowLockSet()->end() &&
      !txn->GetExclusiveRowLockSet()->at(oid).empty()) {
    return true;
  }
  return false;
}

auto LockManager::GetRowLockModeInTxn(Transaction *txn, const table_oid_t &oid, const RID &rid, LockMode* lock_mode) -> bool {
  bool rt = false;
  if (txn->GetSharedRowLockSet()->find(oid) != txn->GetSharedRowLockSet()->end() &&
      txn->GetSharedRowLockSet()->at(oid).find(rid) != txn->GetSharedRowLockSet()->at(oid).end()) {
    *lock_mode = LockMode::SHARED;
    rt = true;
  } else if (txn->GetExclusiveRowLockSet()->find(oid) != txn->GetExclusiveRowLockSet()->end() &&
             txn->GetExclusiveRowLockSet()->at(oid).find(rid) != txn->GetExclusiveRowLockSet()->at(oid).end()) {
    *lock_mode = LockMode::EXCLUSIVE;
    rt = true;
  }
  return rt;
}

auto LockManager::GetOrCreateLockRequestQueue(const table_oid_t &oid) -> std::shared_ptr<LockRequestQueue> {
  auto iter = table_lock_map_.find(oid);
  if (iter == table_lock_map_.end()) {
    auto result = table_lock_map_.insert({oid, std::make_shared<LockRequestQueue>()});
    BUSTUB_ASSERT(result.second, "table_lock_map insertion failed");
    iter = result.first;  
  }
  return iter->second;
}

auto LockManager::GetOrCreateLockRequestQueue(const RID &rid) -> std::shared_ptr<LockRequestQueue> {
  auto iter = row_lock_map_.find(rid);
  if (iter == row_lock_map_.end()) {
    auto result = row_lock_map_.insert({rid, std::make_shared<LockRequestQueue>()});
    BUSTUB_ASSERT(result.second, "row_lock_map insertion failed");
    iter = result.first;  
  }
  return iter->second;
}

auto LockManager::FindLockRequest(LockRequestQueue* lock_request_queue, const txn_id_t &txn_id, const table_oid_t &oid) -> std::list<std::shared_ptr<LockRequest>>::iterator {
  auto request_queue_iter = lock_request_queue->request_queue_.begin();
  for (; request_queue_iter != lock_request_queue->request_queue_.end(); request_queue_iter++) {
    if ((*request_queue_iter)->txn_id_ == txn_id && (*request_queue_iter)->oid_ == oid) {
      break;
    }
  }
  return request_queue_iter;
}

auto LockManager::FindLockRequest(LockRequestQueue* lock_request_queue, const txn_id_t &txn_id, const RID &rid) -> std::list<std::shared_ptr<LockRequest>>::iterator {
  auto request_queue_iter = lock_request_queue->request_queue_.begin();
  for (; request_queue_iter != lock_request_queue->request_queue_.end(); request_queue_iter++) {
    if ((*request_queue_iter)->txn_id_ == txn_id && (*request_queue_iter)->rid_ == rid) {
      break;
    }
  }
  return request_queue_iter;
}
}  // namespace bustub
