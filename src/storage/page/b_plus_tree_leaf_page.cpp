/*
 * @Author: ghost 13038089398@163.com
 * @Date: 2023-09-06 11:46:19
 * @LastEditors: ghost 13038089398@163.com
 * @LastEditTime: 2023-10-06 19:26:22
 * @FilePath: /cmu15445/src/storage/page/b_plus_tree_leaf_page.cpp
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置:
 * https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
//===----------------------------------------------------------------------===//
//
//                         CMU-DB Project (15-445/645)
//                         ***DO NO SHARE PUBLICLY***
//
// Identification: src/page/b_plus_tree_leaf_page.cpp
//
// Copyright (c) 2018, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <sstream>

#include "common/config.h"
#include "common/exception.h"
#include "common/rid.h"
#include "storage/page/b_plus_tree_leaf_page.h"

namespace bustub {

/*****************************************************************************
 * HELPER METHODS AND UTILITIES
 *****************************************************************************/

/**
 * Init method after creating a new leaf page
 * Including set page type, set current size to zero, set next page id and set max size
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::Init(int max_size) {
  SetPageType(IndexPageType::LEAF_PAGE);
  SetSize(0);
  SetMaxSize(max_size);
  SetNextPageId(INVALID_PAGE_ID);
}

/**
 * Helper methods to set/get next page id
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetNextPageId() const -> page_id_t { return next_page_id_; }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::SetNextPageId(page_id_t next_page_id) { next_page_id_ = next_page_id; }

/*
 * Helper method to find and return the key associated with input "index"(a.k.a
 * array offset)
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::KeyAt(int index) const -> KeyType {
  // replace with your own code
  return array_[index].first;
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::ValueAt(int index) const -> ValueType {
  // replace with your own code
  return array_[index].second;
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::PairAt(int index) const -> const MappingType & { return array_[index]; }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::SetPairAt(const MappingType &pair, int index) { array_[index] = pair; }

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::InsertAt(const KeyType &key, const ValueType &value, int index) -> bool {
  // insert
  for (int i = GetSize(); i > index; --i) {
    array_[i] = array_[i - 1];
  }
  array_[index] = MappingType(key, value);
  IncreaseSize(1);
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::InsertAtAndSplitTo(const KeyType &key, const ValueType &value, int index,
                                                    BPlusTreeLeafPage<KeyType, ValueType, KeyComparator> *dst) {
  auto buffer = new MappingType[GetSize() + 1];
  for (int i = 0; i < index; ++i) {
    buffer[i] = array_[i];
  }
  buffer[index] = MappingType(key, value);
  for (int i = index; i < GetSize(); ++i) {
    buffer[i + 1] = array_[i];
  }
  int end = GetSize() + 1;
  int start = (end + 1) >> 1;
  for (int i = 0; i < start; ++i) {
    array_[i] = buffer[i];
  }
  for (int i = start; i < end; ++i) {
    dst->array_[i - start] = buffer[i];
  }
  SetSize(start);
  dst->SetSize(end - start);
  delete[] buffer;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::RemoveAt(int index) {
  for (int i = index; i < GetSize() - 1; ++i) {
    array_[i] = array_[i + 1];
  }
  IncreaseSize(-1);
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::BorrowFromLeft(B_PLUS_TREE_LEAF_PAGE_TYPE *left_leaf_page) -> bool {
  if (left_leaf_page->GetSize() <= GetMinSize()) {
    return false;
  }
  for (int i = GetSize(); i > 0; --i) {
    array_[i] = array_[i - 1];
  }
  array_[0] = left_leaf_page->array_[left_leaf_page->GetSize() - 1];
  SetSize(GetSize() + 1);
  left_leaf_page->SetSize(left_leaf_page->GetSize() - 1);
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::BorrowFromRight(B_PLUS_TREE_LEAF_PAGE_TYPE *right_leaf_page) -> bool {
  if (right_leaf_page->GetSize() <= GetMinSize()) {
    return false;
  }
  array_[GetSize()] = right_leaf_page->array_[0];
  for (int i = 0; i < right_leaf_page->GetSize() - 1; ++i) {
    right_leaf_page->array_[i] = right_leaf_page->array_[i + 1];
  }
  SetSize(GetSize() + 1);
  right_leaf_page->SetSize(right_leaf_page->GetSize() - 1);
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MergeToLeft(B_PLUS_TREE_LEAF_PAGE_TYPE *left_leaf_page) {
  int merge_index = left_leaf_page->GetSize();
  for (int i = 0; i < GetSize(); ++i) {
    left_leaf_page->array_[merge_index++] = array_[i];
  }
  left_leaf_page->SetSize(merge_index);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MergeToRight(B_PLUS_TREE_LEAF_PAGE_TYPE *right_leaf_page) {
  int merge_index = right_leaf_page->GetSize() + GetSize();
  int j = merge_index;
  for (int i = right_leaf_page->GetSize() - 1; i >= 0; --i) {
    right_leaf_page->array_[--j] = right_leaf_page->array_[i];
  }
  for (int i = GetSize(); i >= 0; i--) {
    right_leaf_page->array_[--j] = array_[i];
  }
  right_leaf_page->SetSize(merge_index);
}

template class BPlusTreeLeafPage<GenericKey<4>, RID, GenericComparator<4>>;
template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>>;
template class BPlusTreeLeafPage<GenericKey<16>, RID, GenericComparator<16>>;
template class BPlusTreeLeafPage<GenericKey<32>, RID, GenericComparator<32>>;
template class BPlusTreeLeafPage<GenericKey<64>, RID, GenericComparator<64>>;
}  // namespace bustub
