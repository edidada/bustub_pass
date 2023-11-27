/*
 * @Author: ghost 13038089398@163.com
 * @Date: 2023-09-06 10:04:14
 * @LastEditors: ghost 13038089398@163.com
 * @LastEditTime: 2023-09-27 14:00:46
 * @FilePath: /cmu15445/src/include/storage/page/b_plus_tree_page.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置:
 * https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
//===----------------------------------------------------------------------===//
//
//                         CMU-DB Project (15-445/645)
//                         ***DO NO SHARE PUBLICLY***
//
// Identification: src/include/page/b_plus_tree_page.h
//
// Copyright (c) 2018, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cassert>
#include <climits>
#include <cstdlib>
#include <string>

#include "buffer/buffer_pool_manager.h"
#include "storage/index/generic_key.h"

namespace bustub {

#define MappingType std::pair<KeyType, ValueType>

#define INDEX_TEMPLATE_ARGUMENTS template <typename KeyType, typename ValueType, typename KeyComparator>

// define page type enum
enum class IndexPageType { INVALID_INDEX_PAGE = 0, LEAF_PAGE, INTERNAL_PAGE };

/**
 * Both internal and leaf page are inherited from this page.
 *
 * It actually serves as a header part for each B+ tree page and
 * contains information shared by both leaf page and internal page.
 *
 * Header format (size in byte, 12 bytes in total):
 * ----------------------------------------------------------------------------
 * | PageType (4) | CurrentSize (4) | MaxSize (4) |
 * ----------------------------------------------------------------------------
 */
class BPlusTreePage {
 public:
  // Delete all constructor / destructor to ensure memory safety
  BPlusTreePage() = delete;
  BPlusTreePage(const BPlusTreePage &other) = delete;
  ~BPlusTreePage() = delete;

  auto IsLeafPage() const -> bool;
  void SetPageType(IndexPageType page_type);

  auto GetSize() const -> int;
  void SetSize(int size);
  void IncreaseSize(int amount);

  auto GetMaxSize() const -> int;
  void SetMaxSize(int max_size);
  auto GetMinSize() const -> int;

 private:
  // member variable, attributes that both internal and leaf page share
  IndexPageType page_type_;
  int size_;
  int max_size_;
};

}  // namespace bustub
