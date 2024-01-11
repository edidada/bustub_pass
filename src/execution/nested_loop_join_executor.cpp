//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nested_loop_join_executor.cpp
//
// Identification: src/execution/nested_loop_join_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/nested_loop_join_executor.h"
#include <cstddef>
#include <memory>
#include "binder/table_ref/bound_join_ref.h"
#include "common/config.h"
#include "common/exception.h"
#include "storage/table/table_heap.h"
#include "storage/table/tuple.h"
#include "type/type.h"
#include "type/value_factory.h"

namespace bustub {

NestedLoopJoinExecutor::NestedLoopJoinExecutor(ExecutorContext *exec_ctx, const NestedLoopJoinPlanNode *plan,
                                               std::unique_ptr<AbstractExecutor> &&left_executor,
                                               std::unique_ptr<AbstractExecutor> &&right_executor)
    : AbstractExecutor(exec_ctx), plan_(plan),
      left_executor_(std::move(left_executor)),
      right_executor_(std::move(right_executor)),
      result_table_(std::make_unique<TableHeap>(exec_ctx_->GetBufferPoolManager())),
      result_table_iter_(nullptr) {
  if (!(plan->GetJoinType() == JoinType::LEFT || plan->GetJoinType() == JoinType::INNER)) {
    // Note for 2023 Spring: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
}

void NestedLoopJoinExecutor::Init() { 
  // If has inited, just clear iterator
  if (inited_) {
    result_table_iter_ = std::make_unique<TableIterator>(result_table_->MakeIterator());
    return;
  }

  // If not inited, compute the result table
  Tuple left_tuple{};
  Tuple right_tuple{};
  RID left_rid{};
  RID right_rid{};
  auto join_expr = plan_->Predicate();
  std::vector<Value> values{};
  values.reserve(GetOutputSchema().GetColumnCount());

  left_executor_->Init();
  while (left_executor_->Next(&left_tuple, &left_rid)) {

    right_executor_->Init();
    bool has_match = false;

    while (right_executor_->Next(&right_tuple, &right_rid)) {
      auto value = join_expr->EvaluateJoin(&left_tuple, left_executor_->GetOutputSchema(), &right_tuple,
                                           right_executor_->GetOutputSchema());
      if (!value.IsNull() && value.GetAs<bool>()) {
        values.clear();
        has_match = true;
        for (size_t idx = 0; idx < left_executor_->GetOutputSchema().GetColumnCount(); ++idx) {
          values.push_back(left_tuple.GetValue(&left_executor_->GetOutputSchema(), idx));
        }
        for (size_t idx = 0; idx < right_executor_->GetOutputSchema().GetColumnCount(); ++idx) {
          values.push_back(right_tuple.GetValue(&right_executor_->GetOutputSchema(), idx));
        }
        result_table_->InsertTuple({INVALID_TXN_ID, INVALID_TXN_ID, false}, Tuple(values, &GetOutputSchema()));
      }
    }

    if (!has_match && plan_->GetJoinType() == JoinType::LEFT) {
      values.clear();
      for (size_t idx = 0; idx < left_executor_->GetOutputSchema().GetColumnCount(); ++idx) {
        values.push_back(left_tuple.GetValue(&left_executor_->GetOutputSchema(), idx));
      }
      for (size_t idx = 0; idx < right_executor_->GetOutputSchema().GetColumnCount(); ++idx) {
        values.push_back(ValueFactory::GetNullValueByType(right_executor_->GetOutputSchema().GetColumn(idx).GetType()));
      }
      result_table_->InsertTuple({INVALID_TXN_ID, INVALID_TXN_ID, false}, Tuple(values, &GetOutputSchema()));
    }
  }
  
  inited_ = true;
  result_table_iter_ = std::make_unique<TableIterator>(result_table_->MakeIterator());
}

auto NestedLoopJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool { 
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
