/*
 * @Author: ghost 13038089398@163.com
 * @Date: 2023-09-27 13:46:37
 * @LastEditors: ghost 13038089398@163.com
 * @LastEditTime: 2023-10-06 19:00:13
 * @FilePath: /cmu15445/mnt/d/project/cmu15445/src/include/storage/page/b_plus_tree_internal_page.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置:
 * https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
//===----------------------------------------------------------------------===//
//
//                         CMU-DB Project (15-445/645)
//                         ***DO NO SHARE PUBLICLY***
//
// Identification: src/include/page/b_plus_tree_internal_page.h
//
// Copyright (c) 2018, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#pragma once

#include <queue>
#include <string>

#include "storage/page/b_plus_tree_page.h"

namespace bustub {

#define B_PLUS_TREE_INTERNAL_PAGE_TYPE BPlusTreeInternalPage<KeyType, ValueType, KeyComparator>
#define INTERNAL_PAGE_HEADER_SIZE 12
#define INTERNAL_PAGE_SIZE ((BUSTUB_PAGE_SIZE - INTERNAL_PAGE_HEADER_SIZE) / (sizeof(MappingType)))
/**
 * Store n indexed keys and n+1 child pointers (page_id) within internal page.
 * Pointer PAGE_ID(i) points to a subtree in which all keys K satisfy:
 * K(i) <= K < K(i+1).
 * NOTE: since the number of keys does not equal to number of child pointers,
 * the first key always remains invalid. That is to say, any search/lookup
 * should ignore the first key.
 *
 * Internal page format (keys are stored in increasing order):
 *  --------------------------------------------------------------------------
 * | HEADER | KEY(1)+PAGE_ID(1) | KEY(2)+PAGE_ID(2) | ... | KEY(n)+PAGE_ID(n) |
 *  --------------------------------------------------------------------------
 */
INDEX_TEMPLATE_ARGUMENTS
class BPlusTreeInternalPage : public BPlusTreePage {
 public:
  // Deleted to disallow initialization
  BPlusTreeInternalPage() = delete;
  BPlusTreeInternalPage(const BPlusTreeInternalPage &other) = delete;

  /**
   * Writes the necessary header information to a newly created page, must be called after
   * the creation of a new page to make a valid BPlusTreeInternalPage
   * @param max_size Maximal size of the page
   */
  void Init(int max_size = INTERNAL_PAGE_SIZE);

  /**
   * @param index The index of the key to get. Index must be non-zero.
   * @return Key at index
   */
  auto KeyAt(int index) const -> KeyType;

  /**
   *
   * @param index the index
   * @return the value at the index
   */
  auto ValueAt(int index) const -> ValueType;

  auto InsertAt(const KeyType &key, const ValueType &value, int index) -> bool;

  void SetKeyAt(const KeyType &key, int index);

  void SetValueAt(const ValueType &value, int index);

  void RemoveAt(int index);

  // remove
  auto BorrowFromLeft(BPlusTreeInternalPage<KeyType, ValueType, KeyComparator> *brother_page_left, int borrow_cnt)
      -> bool;
  auto BorrowFromRight(BPlusTreeInternalPage<KeyType, ValueType, KeyComparator> *brother_page_right, int borrow_cnt)
      -> bool;
  void MergeToLeft(BPlusTreeInternalPage<KeyType, ValueType, KeyComparator> *brother_page_left);
  void MergeToRight(BPlusTreeInternalPage<KeyType, ValueType, KeyComparator> *brother_page_right);

  void SplitTo(BPlusTreeInternalPage<KeyType, ValueType, KeyComparator> *dst);
  /**
   * @brief For test only, return a string representing all keys in
   * this internal page, formatted as "(key1,key2,key3,...)"
   *
   * @return std::string
   */
  auto ToString() const -> std::string {
    std::string kstr = "(";
    bool first = true;

    // first key of internal page is always invalid
    for (int i = 1; i < GetSize(); i++) {
      KeyType key = KeyAt(i);
      if (first) {
        first = false;
      } else {
        kstr.append(",");
      }
      kstr.append(std::to_string(key.ToString()));
    }
    kstr.append(")");

    return kstr;
  }

 private:
  // Flexible array member for page data.
  MappingType array_[0];
};
}  // namespace bustub
