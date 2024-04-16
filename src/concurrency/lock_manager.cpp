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
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <stack>

#include "common/config.h"
#include "common/macros.h"
#include "concurrency/transaction.h"
#include "concurrency/transaction_manager.h"

namespace bustub {

auto LockManager::LockTable(Transaction *txn, LockMode lock_mode, const table_oid_t &oid) -> bool { 
  std::unique_lock<std::mutex> table_lock_map_latch(table_lock_map_latch_);

  // 2PL check if the txn can take the lock
  if (!CanTxnTakeLock(txn, lock_mode)) {
    return false;
  }

  // try to get the lock_request_queue on the table
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

  // wait until the lock is aborted or granted
  while (txn->GetState() != TransactionState::ABORTED && !new_lock_request->granted_) {
    lock_request_queue_ptr->cv_.wait(lock_request_queue_latch);
  }

  // if the lock is aborted (by deadlock detection), remove this lock_request, and call Abort()
  if (txn->GetState() == TransactionState::ABORTED) {
    request_queue_iter = FindLockRequest(lock_request_queue_ptr.get(), txn->GetTransactionId(), oid);
    lock_request_queue_ptr->request_queue_.erase(request_queue_iter);
    GrantNewLocksIfPossible(lock_request_queue_ptr.get());
    lock_request_queue_ptr->cv_.notify_all();
    return false;
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
  std::unique_lock<std::mutex> table_lock_map_latch(table_lock_map_latch_);

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
  lock_request_queue_ptr->cv_.notify_all();
  return true;
}

auto LockManager::LockRow(Transaction *txn, LockMode lock_mode, const table_oid_t &oid, const RID &rid) -> bool {
  std::unique_lock<std::mutex> row_lock_map_latch(row_lock_map_latch_);

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
      // (lock_mode == LockMode::SHARED && table_lock_mode != LockMode::INTENTION_SHARED && table_lock_mode != LockMode::SHARED) || 
      (lock_mode == LockMode::EXCLUSIVE && table_lock_mode != LockMode::INTENTION_EXCLUSIVE && table_lock_mode != LockMode::SHARED_INTENTION_EXCLUSIVE && table_lock_mode != LockMode::EXCLUSIVE)) {
    txn->SetState(TransactionState::ABORTED);
    throw TransactionAbortException(txn->GetTransactionId(), AbortReason::TABLE_LOCK_NOT_PRESENT);
    return false;
  }

  // try to get the lock_request_queue on the row
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

  // wait until the lock is aborted or granted
  while (txn->GetState() != TransactionState::ABORTED && !new_lock_request->granted_) {
    lock_request_queue_ptr->cv_.wait(lock_request_queue_latch);
  }

  // if the lock is aborted (by deadlock detection), remove this lock_request, and call Abort()
  if (txn->GetState() == TransactionState::ABORTED) {
    request_queue_iter = FindLockRequest(lock_request_queue_ptr.get(), txn->GetTransactionId(), oid);
    lock_request_queue_ptr->request_queue_.erase(request_queue_iter);
    GrantNewLocksIfPossible(lock_request_queue_ptr.get());
    lock_request_queue_ptr->cv_.notify_all();
    return false;
  }

  // if the lock is granted, add the lock to txn
  AddRowLockInTxn(txn, lock_mode, oid, rid);
  if (lock_request_queue_ptr->upgrading_ == txn->GetTransactionId()) {
    lock_request_queue_ptr->upgrading_ = INVALID_TXN_ID;
  }
  return true;
}

auto LockManager::UnlockRow(Transaction *txn, const table_oid_t &oid, const RID &rid, bool force) -> bool {
  std::unique_lock<std::mutex> row_lock_map_latch(row_lock_map_latch_);

  // check if the txn holds the row lock
  LockMode lock_mode;
  // LOG_DEBUG("TABLE %d", oid);
  // for (auto &rid : txn->GetExclusiveRowLockSet()->at(oid)) {
  //   LOG_DEBUG("(RID %d %d)", rid.GetPageId(), rid.GetSlotNum());
  // }
  if (!GetRowLockModeInTxn(txn, oid, rid, &lock_mode)) {
    txn->SetState(TransactionState::ABORTED);
    throw TransactionAbortException(txn->GetTransactionId(), AbortReason::ATTEMPTED_UNLOCK_BUT_NO_LOCK_HELD);
    return false;
  }

  // try to get the lock_request_queue on the row
  auto iter = row_lock_map_.find(rid);
  if (iter == row_lock_map_.end()) {
    return false;
  }
  std::shared_ptr<LockRequestQueue> lock_request_queue_ptr = iter->second;
  std::unique_lock<std::mutex> lock_request_queue_latch(lock_request_queue_ptr->latch_);
  row_lock_map_latch.unlock();

  // try to unlock the row
  auto request_queue_iter = FindLockRequest(lock_request_queue_ptr.get(), txn->GetTransactionId(), rid);
  BUSTUB_ASSERT(request_queue_iter != lock_request_queue_ptr->request_queue_.end(), "unlock row without lock");
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
  lock_request_queue_ptr->cv_.notify_all();
  return true;
}

void LockManager::UnlockAll() {
  // You probably want to unlock all table and txn locks here.
  std::unique_lock<std::mutex> row_lock_map_latch(row_lock_map_latch_);
  std::unique_lock<std::mutex> table_lock_map_latch(table_lock_map_latch_);
  // Unlock all row locks
  for (auto &lock_request_pair : row_lock_map_) {
    auto &lock_request_queue_ptr = lock_request_pair.second;
    std::unique_lock<std::mutex> lock_request_queue_latch(lock_request_queue_ptr->latch_);
    if (lock_request_queue_ptr->request_queue_.empty()) {
      continue;
    }
    auto lock_request_queue_iter = lock_request_queue_ptr->request_queue_.begin();
    while (!lock_request_queue_ptr->request_queue_.empty()) {
      // auto &lock_request_ptr = *lock_request_queue_iter;
      // if (lock_request_ptr->granted_) {
      //   RemoveRowLockInTxn(txn_manager_->GetTransaction(lock_request_ptr->txn_id_), 
      //                     lock_request_ptr->lock_mode_, 
      //                     lock_request_ptr->oid_, 
      //                     lock_request_ptr->rid_);
      // }
      lock_request_queue_iter = lock_request_queue_ptr->request_queue_.erase(lock_request_queue_iter);
    }
  }
  // Unlock all table locks
  for (auto &lock_request_pair : table_lock_map_) {
    auto &lock_request_queue_ptr = lock_request_pair.second;
    std::unique_lock<std::mutex> lock_request_queue_latch(lock_request_queue_ptr->latch_);
    if (lock_request_queue_ptr->request_queue_.empty()) {
      continue;
    }
    auto lock_request_queue_iter = lock_request_queue_ptr->request_queue_.begin();
    while (!lock_request_queue_ptr->request_queue_.empty()) {
      // auto &lock_request_ptr = *lock_request_queue_iter;
      // if (lock_request_ptr->granted_) {
      //   RemoveTableLockInTxn(txn_manager_->GetTransaction(lock_request_ptr->txn_id_), 
      //                     lock_request_ptr->lock_mode_, 
      //                     lock_request_ptr->oid_);
      // }
      lock_request_queue_iter = lock_request_queue_ptr->request_queue_.erase(lock_request_queue_iter);
    }
  }
}

void LockManager::AddEdge(txn_id_t t1, txn_id_t t2) {
  std::unique_lock<std::mutex> waits_for_latch(waits_for_latch_);
  AddEdgeLF(t1, t2);
}

void LockManager::RemoveEdge(txn_id_t t1, txn_id_t t2) {
  std::unique_lock<std::mutex> waits_for_latch(waits_for_latch_);
  RemoveEdgeLF(t1, t2);
}

auto LockManager::HasCycle(txn_id_t *txn_id) -> bool { 
  std::unordered_set<txn_id_t> visited;
  std::unordered_set<txn_id_t> instack;
  std::stack<txn_id_t> visit_trace;
  for (auto riter = alive_txns_.rbegin(); riter != alive_txns_.rend(); ++riter) {
    if (visited.find(*riter) == visited.end()) {
      if (Dfs(*riter, visited, instack, visit_trace)) {
        txn_id_t top = visit_trace.top();
        txn_id_t txn_to_abort = top;
        do {
          visit_trace.pop();
          txn_to_abort = std::max(txn_to_abort, visit_trace.top());
        } while (top != visit_trace.top());
        *txn_id = txn_to_abort;
        return true;
      }
    }
  }
  return false;
}

auto LockManager::Dfs(txn_id_t x, std::unordered_set<txn_id_t> &visited, std::unordered_set<txn_id_t> &instack, std::stack<txn_id_t>& visit_trace) -> bool {
  visit_trace.push(x);
  instack.insert(x);
  visited.insert(x);

  for (auto &y : waits_for_[x]) {
    if (alive_txns_.find(y) != alive_txns_.end()) {
      if (visited.find(y) == visited.end()) {
        auto has_cycle = Dfs(y, visited, instack, visit_trace);
        if (has_cycle) {
          return true;
        }
      } else if (instack.find(y) != instack.end()) {
        visit_trace.push(y);
        return true;
      }
    }
  }

  instack.erase(x);
  visit_trace.pop();
  return false;
}

auto LockManager::GetEdgeList() -> std::vector<std::pair<txn_id_t, txn_id_t>> {
  std::vector<std::pair<txn_id_t, txn_id_t>> edges(0);
  std::unique_lock<std::mutex> waits_for_latch(waits_for_latch_);
  for (auto &waits_for_pair : waits_for_) {
    txn_id_t t1 = waits_for_pair.first;
    for (txn_id_t t2 : waits_for_pair.second) {
      edges.emplace_back(t1, t2);
    }
  }
  return edges;
}

void LockManager::RunCycleDetection() {
  while (enable_cycle_detection_) {
    std::this_thread::sleep_for(cycle_detection_interval);
    {  // TODO(students): detect deadlock
      // build waits_for
      std::unique_lock<std::mutex> waits_for_latch(waits_for_latch_);
      {
        std::unique_lock<std::mutex> row_lock_map_latch(row_lock_map_latch_);
        std::unique_lock<std::mutex> table_lock_map_latch(table_lock_map_latch_);
        for (auto &lock_request_pair : row_lock_map_) {
          auto &lock_request_queue_ptr = lock_request_pair.second;
          std::unique_lock<std::mutex> lock_request_queue_latch(lock_request_queue_ptr->latch_);
          AddEdgesFromQueue(lock_request_queue_ptr.get());
        }
        for (auto &lock_request_pair : table_lock_map_) {
          auto &lock_request_queue_ptr = lock_request_pair.second;
          std::unique_lock<std::mutex> lock_request_queue_latch(lock_request_queue_ptr->latch_);
          AddEdgesFromQueue(lock_request_queue_ptr.get());
        }
      }
      // find cycle
      std::vector<txn_id_t> txn_to_abort_list;
      txn_id_t txn_to_abort;
      while (HasCycle(&txn_to_abort)) {
        txn_to_abort_list.emplace_back(txn_to_abort);
        alive_txns_.erase(txn_to_abort);
      }

      // abort all txns leading to deadlock
      for (auto &txn_id : txn_to_abort_list) {
        txn_manager_->Abort(txn_manager_->GetTransaction(txn_id));
      }

      // notify the aborted txns
      for (auto &txn_id : txn_to_abort_list) {
        txn_wait_map_[txn_id]->cv_.notify_all();
      }

      // clear waits_for_
      waits_for_.clear();
      alive_txns_.clear();
      txn_wait_map_.clear();
    }
  }
}


auto LockManager::AddEdgesFromQueue(LockRequestQueue *lock_request_queue) -> void {
  for (auto iter1 = lock_request_queue->request_queue_.begin(); iter1 != lock_request_queue->request_queue_.end(); ++iter1) {
    if ((*iter1)->granted_ ||
        txn_manager_->GetTransaction((*iter1)->txn_id_)->GetState() == TransactionState::ABORTED) {
      continue;
    }
    alive_txns_.insert((*iter1)->txn_id_);
    txn_wait_map_.insert({(*iter1)->txn_id_, lock_request_queue});
    for (auto iter2 = lock_request_queue->request_queue_.begin(); iter2 != iter1 && (*iter2)->granted_; ++iter2) {
      if (txn_manager_->GetTransaction((*iter2)->txn_id_)->GetState() == TransactionState::ABORTED) {
        continue;
      }
      AddEdgeLF((*iter1)->txn_id_, (*iter2)->txn_id_);
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
  // LOG_DEBUG("TABLE %d", oid);
  // for (auto &rid : txn->GetExclusiveRowLockSet()->at(oid)) {
  //   LOG_DEBUG("(RID %d %d)", rid.GetPageId(), rid.GetSlotNum());
  // }
}

void LockManager::RemoveRowLockInTxn(Transaction *txn, LockMode lock_mode, const table_oid_t &oid, const RID &rid) {
  switch (lock_mode) {
  case LockMode::SHARED:  
    txn->GetSharedRowLockSet()->at(oid).erase(rid);
    break;
  case LockMode::EXCLUSIVE:
    txn->GetExclusiveRowLockSet()->at(oid).erase(rid);
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

void LockManager::AddEdgeLF(txn_id_t t1, txn_id_t t2) {
  auto iter = waits_for_.find(t1);
  if (iter == waits_for_.end()) {
    iter = waits_for_.insert({t1, std::vector<txn_id_t>()}).first;
  }
  if (std::find(iter->second.begin(), iter->second.end(), t2) == iter->second.end()) {
    iter->second.emplace_back(t2);
  }
}

void LockManager::RemoveEdgeLF(txn_id_t t1, txn_id_t t2) {
  auto iter = waits_for_.find(t1);
  if (iter == waits_for_.end()) {
    return;
  }
  (void)std::remove(iter->second.begin(), iter->second.end(), t2);
}

}  // namespace bustub
