#include "execution/executors/sort_executor.h"
#include "common/macros.h"
#include "common/rid.h"
#include "storage/table/tuple.h"

namespace bustub {

SortExecutor::SortExecutor(ExecutorContext *exec_ctx, const SortPlanNode *plan,
                           std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), 
      child_executor_(std::move(child_executor)) {}

void SortExecutor::Init() { 
  if (inited_) {
    idx_ = 0;
    return;
  }

  child_executor_->Init();

  Tuple child_tuple{};
  RID rid{};

  std::vector<OrderByType> sort_types{};
  sort_types.reserve(plan_->GetOrderBy().size());
  for (const auto &order_by : plan_->GetOrderBy()) {
    sort_types.push_back(order_by.first);
  }
  
  std::vector<Value> sort_keys{};
  sort_keys.reserve(plan_->GetOrderBy().size());
  while (child_executor_->Next(&child_tuple, &rid)) {
    sort_keys.clear();
    for (const auto &order_by : plan_->GetOrderBy()) {
      sort_keys.push_back(order_by.second->Evaluate(&child_tuple, child_executor_->GetOutputSchema()));
    }
    sort_pairs_.emplace_back(sort_keys, sort_types, child_tuple, rid);
  }

  std::sort(sort_pairs_.begin(), sort_pairs_.end(), SortComparator());
  inited_ = true;
}

auto SortExecutor::Next(Tuple *tuple, RID *rid) -> bool {  
  if (idx_ >= sort_pairs_.size()) {
    return false;
  }

  *tuple = sort_pairs_[idx_].tuple_;
  *rid = sort_pairs_[idx_].rid_;

  ++idx_;

  return true;
}

}  // namespace bustub
