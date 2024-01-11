//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// aggregation_executor.cpp
//
// Identification: src/execution/aggregation_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#include <memory>
#include <utility>
#include <vector>

#include "common/rid.h"
#include "execution/executors/aggregation_executor.h"
#include "execution/plans/aggregation_plan.h"
#include "storage/table/tuple.h"
#include "type/type.h"

namespace bustub {

AggregationExecutor::AggregationExecutor(ExecutorContext *exec_ctx, const AggregationPlanNode *plan,
                                         std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)),
      aht_(plan->GetAggregates(), plan->GetAggregateTypes()),
      aht_iterator_(aht_.Begin()) {}

void AggregationExecutor::Init() {
    if (inited_) {
        aht_iterator_ = aht_.Begin();
        return;
    }

    // Initialize the child executor
    child_executor_->Init();

    Tuple child_tuple{};
    RID child_rid{};
    // Empty table
    bool status = child_executor_->Next(&child_tuple, &child_rid);
    if (!status && plan_->GetGroupBys().empty()) {
        AggregateKey empty_key{};
        AggregateValue empty_value = aht_.GenerateInitialAggregateValue();
        aht_.InsertCombine(empty_key, empty_value);
    }
    // Initialize the hash table
    while (status) {
        aht_.InsertCombine(MakeAggregateKey(&child_tuple), MakeAggregateValue(&child_tuple));
        status = child_executor_->Next(&child_tuple, &child_rid);
    }
    aht_iterator_ = aht_.Begin();

    inited_ = true;
}

auto AggregationExecutor::Next(Tuple *tuple, RID *rid) -> bool { 
    if (aht_iterator_ == aht_.End()) {
        return false;
    }
    std::vector<Value> values{};
    values.reserve(GetOutputSchema().GetColumnCount());
    for (const auto &value : aht_iterator_.Key().group_bys_) {
        values.push_back(value);
    }
    for (const auto &value : aht_iterator_.Val().aggregates_) {
        values.push_back(value);
    }
    *tuple = Tuple{values, &GetOutputSchema()};
    ++aht_iterator_;
    return true;    
}

auto AggregationExecutor::GetChildExecutor() const -> const AbstractExecutor * { return child_executor_.get(); }

}  // namespace bustub
