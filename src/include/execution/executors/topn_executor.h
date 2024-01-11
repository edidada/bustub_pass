//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// topn_executor.h
//
// Identification: src/include/execution/executors/topn_executor.h
//
// Copyright (c) 2015-2022, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "execution/executor_context.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/seq_scan_plan.h"
#include "execution/plans/topn_plan.h"
#include "storage/table/tuple.h"
#include "type/value.h"

namespace bustub {

struct TopNPair {
  std::vector<Value> sort_keys_;
  std::vector<OrderByType> sort_types_;
  Tuple tuple_;
  RID rid_;

  TopNPair() = default;
  TopNPair(std::vector<Value> sort_keys, std::vector<OrderByType> sort_types, Tuple tuple, RID rid)
      : sort_keys_(std::move(sort_keys)), sort_types_(std::move(sort_types)), tuple_(std::move(tuple)), rid_(rid) {}

  auto operator<(const TopNPair& rhs) const -> bool{
    for (uint32_t i = 0; i < sort_keys_.size(); ++i) {
      switch (sort_types_[i]) {
        case OrderByType::DEFAULT:
        case OrderByType::ASC:
          if (sort_keys_[i].CompareLessThan(rhs.sort_keys_[i]) == CmpBool::CmpTrue) {
            return true;
          } 
          if (sort_keys_[i].CompareGreaterThan(rhs.sort_keys_[i]) == CmpBool::CmpTrue) {
            return false;
          }
          break;
        case OrderByType::DESC:
          if (sort_keys_[i].CompareGreaterThan(rhs.sort_keys_[i]) == CmpBool::CmpTrue) {
            return true;
          } 
          if (sort_keys_[i].CompareLessThan(rhs.sort_keys_[i]) == CmpBool::CmpTrue) {
            return false;
          }
          break;
        default:
          break;
      }
    }
    return false;
  }

  auto operator>(const TopNPair& rhs) const -> bool{
    for (uint32_t i = 0; i < sort_keys_.size(); i++) {
      switch (sort_types_[i]) {
        case OrderByType::DEFAULT:
        case OrderByType::ASC:
          if (sort_keys_[i].CompareLessThan(rhs.sort_keys_[i]) == CmpBool::CmpTrue) {
            return false;
          } 
          if (sort_keys_[i].CompareGreaterThan(rhs.sort_keys_[i]) == CmpBool::CmpTrue) {
            return true;
          }
          break;
        case OrderByType::DESC:
          if (sort_keys_[i].CompareGreaterThan(rhs.sort_keys_[i]) == CmpBool::CmpTrue) {
            return false;
          } 
          if (sort_keys_[i].CompareLessThan(rhs.sort_keys_[i]) == CmpBool::CmpTrue) {
            return true;
          }
          break;
        default:
          break;
      }
    }
    return false;
  }

  auto operator<=(const TopNPair& rhs) const -> bool{
    return !operator>(rhs);
  }
};

/**
 * The TopNExecutor executor executes a topn.
 */
class TopNExecutor : public AbstractExecutor {
 public:
  /**
   * Construct a new TopNExecutor instance.
   * @param exec_ctx The executor context
   * @param plan The TopN plan to be executed
   */
  TopNExecutor(ExecutorContext *exec_ctx, const TopNPlanNode *plan, std::unique_ptr<AbstractExecutor> &&child_executor);

  /** Initialize the TopN */
  void Init() override;

  /**
   * Yield the next tuple from the TopN.
   * @param[out] tuple The next tuple produced by the TopN
   * @param[out] rid The next tuple RID produced by the TopN
   * @return `true` if a tuple was produced, `false` if there are no more tuples
   */
  auto Next(Tuple *tuple, RID *rid) -> bool override;

  /** @return The output schema for the TopN */
  auto GetOutputSchema() const -> const Schema & override { return plan_->OutputSchema(); }

  /** Sets new child executor (for testing only) */
  void SetChildExecutor(std::unique_ptr<AbstractExecutor> &&child_executor) {
    child_executor_ = std::move(child_executor);
  }

  /** @return The size of top_entries_ container, which will be called on each child_executor->Next(). */
  auto GetNumInHeap() -> size_t;

 private:
  /** The TopN plan node to be executed */
  const TopNPlanNode *plan_;
  /** The child executor from which tuples are obtained */
  std::unique_ptr<AbstractExecutor> child_executor_;

  std::vector<TopNPair> sort_pairs_;

  uint32_t idx_ = 0;

  bool inited_ = false;
};
}  // namespace bustub
