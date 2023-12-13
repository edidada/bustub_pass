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
#include <vector>
#include "storage/table/table_iterator.h"

namespace bustub {

SeqScanExecutor::SeqScanExecutor(ExecutorContext *exec_ctx, const SeqScanPlanNode *plan) 
    : AbstractExecutor(exec_ctx), 
      plan_(plan),
      table_iter_(exec_ctx_->GetCatalog()->GetTable(plan_->GetTableOid())->table_->MakeIterator()) {}

void SeqScanExecutor::Init() {}

auto SeqScanExecutor::Next(Tuple *tuple, RID *rid) -> bool { 
  while (!table_iter_.IsEnd() && table_iter_.GetTuple().first.is_deleted_) {
    ++table_iter_;
  }
  if (table_iter_.IsEnd()) {
    return false;
  }
  *tuple = table_iter_.GetTuple().second;
  if (rid != nullptr) {
    *rid = table_iter_.GetRID();
  } 
  ++table_iter_;
  return true;
}

}  // namespace bustub
