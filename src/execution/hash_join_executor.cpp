//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// hash_join_executor.cpp
//
// Identification: src/execution/hash_join_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/hash_join_executor.h"
#include <vector>
#include "common/rid.h"
#include "execution/executors/aggregation_executor.h"
#include "storage/table/tuple.h"
#include "type/value.h"
#include "type/value_factory.h"

namespace bustub {

HashJoinExecutor::HashJoinExecutor(ExecutorContext *exec_ctx, const HashJoinPlanNode *plan,
                                   std::unique_ptr<AbstractExecutor> &&left_child,
                                   std::unique_ptr<AbstractExecutor> &&right_child)
    : AbstractExecutor(exec_ctx), plan_(plan),
      left_child_(std::move(left_child)),
      right_child_(std::move(right_child)),
      result_table_(std::make_unique<TableHeap>(exec_ctx_->GetBufferPoolManager())),
      result_table_iter_(nullptr) {
  if (!(plan->GetJoinType() == JoinType::LEFT || plan->GetJoinType() == JoinType::INNER)) {
    // Note for 2023 Spring: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
}

void HashJoinExecutor::Init() { 
  left_child_->Init();
  right_child_->Init();

  if (inited_) {
    result_table_iter_ = std::make_unique<TableIterator>(result_table_->MakeIterator());
    return;
  }

  // If not inited, compute the result table
  // build hash table
  SimpleHashJoinHashTable hash_table{};
  Tuple left_tuple{};
  RID left_rid{};
  while (left_child_->Next(&left_tuple, &left_rid)) {
    hash_table.Insert(MakeLeftJoinKey(&left_tuple), MakeLeftJoinValue(&left_tuple));    
  }

  // hash join
  Tuple right_tuple{};
  RID right_rid{};
  std::vector<HashJoinValue> hash_join_values{};
  std::vector<Value> values{};
  values.reserve(GetOutputSchema().GetColumnCount());
  while (right_child_->Next(&right_tuple, &right_rid)) {
    auto right_key = MakeRightJoinKey(&right_tuple);
    if (hash_table.Get(right_key, &hash_join_values)) {
      hash_table.Mark(right_key);
      for (const auto &match_tuple : hash_join_values) {
        values.clear();
        for (const auto &value : match_tuple.tuple_values_) {
          values.push_back(value);
        }
        for (uint32_t idx = 0; idx < right_child_->GetOutputSchema().GetColumnCount(); ++idx) {
          values.push_back(right_tuple.GetValue(&right_child_->GetOutputSchema(), idx));
        }
        Tuple tuple{values, &GetOutputSchema()};
        result_table_->InsertTuple({INVALID_TXN_ID, INVALID_TXN_ID, false}, Tuple(values, &GetOutputSchema()));
      }
    }
  }

  // left join
  if (plan_->GetJoinType() == JoinType::LEFT) {
    auto unmatched_keys = hash_table.GetUnmatchedKeys();
    for (const auto &unmatched_key : unmatched_keys) {
      hash_table.Get(unmatched_key, &hash_join_values);
      for (const auto &match_tuple : hash_join_values) {
        values.clear();
        for (const auto &value : match_tuple.tuple_values_) {
          values.push_back(value);
        }
        for (uint32_t idx = 0; idx < right_child_->GetOutputSchema().GetColumnCount(); ++idx) {
          values.push_back(ValueFactory::GetNullValueByType(right_child_->GetOutputSchema().GetColumn(idx).GetType()));
        }
        Tuple tuple{values, &GetOutputSchema()};
        result_table_->InsertTuple({INVALID_TXN_ID, INVALID_TXN_ID, false}, Tuple(values, &GetOutputSchema()));
      }
    }
  }

  inited_ = true;
  result_table_iter_ = std::make_unique<TableIterator>(result_table_->MakeIterator());
}

auto HashJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool { 
  if (result_table_iter_->IsEnd()) {
    return false;
  }
  *tuple = result_table_iter_->GetTuple().second;
  if (rid != nullptr) {
    *rid = result_table_iter_->GetRID();
  } 
  ++(*result_table_iter_);
  return true;
}

}  // namespace bustub
