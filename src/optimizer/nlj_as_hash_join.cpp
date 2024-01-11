#include <algorithm>
#include <memory>
#include <vector>
#include "catalog/column.h"
#include "catalog/schema.h"
#include "common/exception.h"
#include "common/macros.h"
#include "execution/expressions/abstract_expression.h"
#include "execution/expressions/column_value_expression.h"
#include "execution/expressions/comparison_expression.h"
#include "execution/expressions/constant_value_expression.h"
#include "execution/expressions/logic_expression.h"
#include "execution/plans/abstract_plan.h"
#include "execution/plans/filter_plan.h"
#include "execution/plans/hash_join_plan.h"
#include "execution/plans/nested_loop_join_plan.h"
#include "execution/plans/projection_plan.h"
#include "optimizer/optimizer.h"
#include "type/type_id.h"

namespace bustub {

auto Optimizer::OptimizeNLJAsHashJoin(const AbstractPlanNodeRef &plan) -> AbstractPlanNodeRef {
  // TODO(student): implement NestedLoopJoin -> HashJoin optimizer rule
  // Note for 2023 Spring: You should at least support join keys of the form:
  // 1. <column expr> = <column expr>
  // 2. <column expr> = <column expr> AND <column expr> = <column expr>
  // optimize children with this rule
  std::vector<AbstractPlanNodeRef> children;
  for (const auto &child : plan->GetChildren()) {
    children.emplace_back(OptimizeNLJAsHashJoin(child));
  }
  auto optimized_plan = plan->CloneWithChildren(std::move(children));

  // optimize this node
  if (optimized_plan->GetType() == PlanType::NestedLoopJoin) {
    const auto &nlj_plan = dynamic_cast<const NestedLoopJoinPlanNode &>(*optimized_plan);
    // Has exactly two children
    BUSTUB_ENSURE(nlj_plan.children_.size() == 2, "NLJ should have exactly 2 children.");
    // Check if expr is equal condition where one is for the left table, and one is for the right table.
    if (const auto *expr = dynamic_cast<const ComparisonExpression *>(nlj_plan.Predicate().get()); expr != nullptr) {
      if (expr->comp_type_ == ComparisonType::Equal) {
        if (const auto *left_expr = dynamic_cast<const ColumnValueExpression *>(expr->children_[0].get()); left_expr != nullptr) {
          if (const auto *right_expr = dynamic_cast<const ColumnValueExpression *>(expr->children_[1].get()); right_expr != nullptr) {
            if (1 == (left_expr->GetTupleIdx() ^ right_expr->GetTupleIdx())) {
              std::vector<AbstractExpressionRef> left_keys = {left_expr->GetTupleIdx() == 0 ? expr->children_[0] : expr->children_[1]};
              std::vector<AbstractExpressionRef> right_keys = {left_expr->GetTupleIdx() == 0 ? expr->children_[1] : expr->children_[0]};
              return std::make_shared<HashJoinPlanNode>(
                  nlj_plan.output_schema_, nlj_plan.GetLeftPlan(), nlj_plan.GetRightPlan(), 
                  left_keys,
                  right_keys, nlj_plan.GetJoinType() );
            }
          }
        }
      }
    }

    // Check if expr is logic and condition
    if (const auto *expr = dynamic_cast<const LogicExpression *>(nlj_plan.Predicate().get()); expr != nullptr) {
      if (const auto *left_expr = dynamic_cast<const ComparisonExpression *>(expr->children_[0].get()); left_expr != nullptr) {
        if (const auto *right_expr = dynamic_cast<const ComparisonExpression *>(expr->children_[1].get()); right_expr != nullptr) {
          if (left_expr->comp_type_ == ComparisonType::Equal && right_expr->comp_type_ == ComparisonType::Equal) {
            // check both left and right expr are column value expr
            if (const auto *left_left_expr = dynamic_cast<const ColumnValueExpression *>(left_expr->children_[0].get()); left_left_expr != nullptr) {
              if (const auto *left_right_expr = dynamic_cast<const ColumnValueExpression *>(left_expr->children_[1].get()); left_right_expr != nullptr) {
                if (const auto *right_left_expr = dynamic_cast<const ColumnValueExpression *>(right_expr->children_[0].get()); right_left_expr != nullptr) {
                  if (const auto *right_right_expr = dynamic_cast<const ColumnValueExpression *>(right_expr->children_[1].get()); right_right_expr != nullptr) {
                    if (1 == (left_left_expr->GetTupleIdx() ^ left_right_expr->GetTupleIdx()) &&
                        1 == (right_left_expr->GetTupleIdx() ^ right_right_expr->GetTupleIdx())) {
                      std::vector<AbstractExpressionRef> left_keys{};
                      std::vector<AbstractExpressionRef> right_keys{};
                      if (left_left_expr->GetTupleIdx() == 0) {
                        left_keys.emplace_back(left_expr->children_[0]);
                        right_keys.emplace_back(left_expr->children_[1]);
                      } else {
                        left_keys.emplace_back(left_expr->children_[1]);
                        right_keys.emplace_back(left_expr->children_[0]);
                      }
                      if (right_left_expr->GetTupleIdx() == 0) {
                        left_keys.emplace_back(right_expr->children_[0]);
                        right_keys.emplace_back(right_expr->children_[1]);
                      } else {
                        left_keys.emplace_back(right_expr->children_[1]);
                        right_keys.emplace_back(right_expr->children_[0]);
                      }
                      return std::make_shared<HashJoinPlanNode>(
                          nlj_plan.output_schema_, nlj_plan.GetLeftPlan(), nlj_plan.GetRightPlan(), 
                          left_keys,
                          right_keys, nlj_plan.GetJoinType() );
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }

  return optimized_plan;
}

}  // namespace bustub
