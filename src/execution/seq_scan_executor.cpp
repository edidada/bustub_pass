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
  table_iter_ = std::make_unique<TableIterator>(exec_ctx_->GetCatalog()->GetTable(plan_->table_oid_)->table_->MakeIterator());
}

auto SeqScanExecutor::Next(Tuple *tuple, RID *rid) -> bool { 
  while (!table_iter_->IsEnd() && table_iter_->GetTuple().first.is_deleted_) {
    ++(*table_iter_);
  }
  if (table_iter_->IsEnd()) {
    return false;
  }
  *tuple = table_iter_->GetTuple().second;
  if (rid != nullptr) {
    *rid = table_iter_->GetRID();
  } 
  ++(*table_iter_);
  return true;
}

}  // namespace bustub
