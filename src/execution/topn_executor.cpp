#include "execution/executors/topn_executor.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include "common/logger.h"

namespace bustub {

TopNExecutor::TopNExecutor(ExecutorContext *exec_ctx, const TopNPlanNode *plan,
                           std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), 
      child_executor_(std::move(child_executor)){}

void TopNExecutor::Init() {
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

    // find topN
    size_t n = plan_->GetN();
    uint32_t start = 0;
    uint32_t end = sort_pairs_.size() - 1;
    if (n < sort_pairs_.size()) {
        // quick sort select
        while (start < end) {
            std::swap(sort_pairs_[start], sort_pairs_[start + rand() % (end - start + 1)]);
            uint32_t div = start;
            for (uint32_t i = start + 1; i <= end; ++i) {
                if (sort_pairs_[i] < sort_pairs_[start]) {
                    std::swap(sort_pairs_[++div], sort_pairs_[i]);
                }
            }
            std::swap(sort_pairs_[div], sort_pairs_[start]);

            if (div == n - 1) {
                break;
            }

            if (div > n - 1) {
                end = div - 1;
            } else {
                start = div + 1;
            }
        }
    }

    // sort topN
    sort_pairs_.resize(std::min(sort_pairs_.size(), n));
    std::sort(sort_pairs_.begin(), sort_pairs_.end());
    inited_ = true;
}

auto TopNExecutor::Next(Tuple *tuple, RID *rid) -> bool { 
  if (idx_ >= sort_pairs_.size()) {
    return false;
  }

  *tuple = sort_pairs_[idx_].tuple_;
  *rid = sort_pairs_[idx_].rid_;

  ++idx_;

  return true;
}

auto TopNExecutor::GetNumInHeap() -> size_t { 
    uint32_t rt = std::min(sort_pairs_.size(), plan_->GetN()) - idx_; 
    LOG_DEBUG("sort_pairs_.size() = %lu, plan_->GetN() = %lu, idx_ = %u, num_in_heap = %u", sort_pairs_.size(), plan_->GetN(), idx_, rt);
    return rt;
}

}  // namespace bustub
