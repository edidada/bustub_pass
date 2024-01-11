//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// index_scan_executor.cpp
//
// Identification: src/execution/index_scan_executor.cpp
//
// Copyright (c) 2015-19, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#include "execution/executors/index_scan_executor.h"
#include <memory>
#include <utility>
#include "storage/index/b_plus_tree_index.h"
#include "storage/table/tuple.h"

namespace bustub {
IndexScanExecutor::IndexScanExecutor(ExecutorContext *exec_ctx, const IndexScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan),
      tree_(dynamic_cast<BPlusTreeIndexForTwoIntegerColumn *>(exec_ctx->GetCatalog()->GetIndex(plan->GetIndexOid())->index_.get())),
      index_iter_(nullptr) {}

void IndexScanExecutor::Init() {
    index_iter_ = std::make_unique<BPlusTreeIndexIteratorForTwoIntegerColumn>(tree_->GetBeginIterator());
}

auto IndexScanExecutor::Next(Tuple *tuple, RID *rid) -> bool { 
    const auto index_info = exec_ctx_->GetCatalog()->GetIndex(plan_->GetIndexOid());
    const auto table_info = exec_ctx_->GetCatalog()->GetTable(index_info->table_name_);
    std::pair<TupleMeta, Tuple> tuplemeta_and_tuple;
    while (!index_iter_->IsEnd()) {
        tuplemeta_and_tuple = table_info->table_->GetTuple((**index_iter_).second); 
        if (!tuplemeta_and_tuple.first.is_deleted_) {
            break;
        }
        ++(*index_iter_);
    }
    if (index_iter_->IsEnd()) {
        return false;
    }
    if (rid != nullptr) {
        *rid = tuplemeta_and_tuple.second.GetRid();
    }
    *tuple = tuplemeta_and_tuple.second;
    ++(*index_iter_);
    return true;
}

}  // namespace bustub
