/*
 * @Author: ghost 13038089398@163.com
 * @Date: 2023-10-04 14:18:17
 * @LastEditors: ghost 13038089398@163.com
 * @LastEditTime: 2023-10-04 15:11:19
 * @FilePath: /cmu15445/src/include/storage/index/index_iterator.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置:
 * https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
//===----------------------------------------------------------------------===//
//
//                         CMU-DB Project (15-445/645)
//                         ***DO NO SHARE PUBLICLY***
//
// Identification: src/include/index/index_iterator.h
//
// Copyright (c) 2018, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
/**
 * index_iterator.h
 * For range scan of b+ tree
 */
#pragma once
#include "storage/page/b_plus_tree_leaf_page.h"

namespace bustub {

#define INDEXITERATOR_TYPE IndexIterator<KeyType, ValueType, KeyComparator>

INDEX_TEMPLATE_ARGUMENTS
class IndexIterator {
 public:
  // you may define your own constructor based on your member variables
  IndexIterator(BufferPoolManager *bpm, ReadPageGuard &&read_page_guard, int index);
  ~IndexIterator();

  auto IsEnd() -> bool;

  auto operator*() -> const MappingType &;

  auto operator++() -> IndexIterator &;

  auto operator==(const IndexIterator &itr) const -> bool;

  auto operator!=(const IndexIterator &itr) const -> bool;

 private:
  // add your own private member variables here
  BufferPoolManager *bpm_;
  ReadPageGuard read_page_guard_;
  const BPlusTreeLeafPage<KeyType, ValueType, KeyComparator> *read_page_;
  int index_;
};

}  // namespace bustub
