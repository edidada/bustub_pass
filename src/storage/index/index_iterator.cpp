/**
 * index_iterator.cpp
 */
#include <cassert>
#include <iostream>
#include <thread>

#include "common/config.h"
#include "common/macros.h"
#include "storage/index/index_iterator.h"

namespace bustub {

/*
 * NOTE: you can change the destructor/constructor method here
 * set your own input parameters
 */
INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::IndexIterator(BufferPoolManager *bpm, ReadPageGuard &&read_page_guard, int index)
    : bpm_(bpm), read_page_guard_(std::move(read_page_guard)), index_(index) {
  // BUSTUB_ASSERT(read_page_guard_.As<BPlusTreePage>()->IsLeafPage(),
  //               "read_page_guard is not leaf page (index_iterator construct)");
  read_page_ = read_page_guard_.As<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>>();
}

INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::~IndexIterator() {}  // NOLINT

INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::IsEnd() -> bool {
  return bpm_ == nullptr && read_page_ == nullptr && index_ == -1;
  // return index_ == read_page_->GetSize() && read_page_->GetNextPageId() == INVALID_PAGE_ID;
}

INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator*() -> const MappingType & { 
  BUSTUB_ASSERT(read_page_ != nullptr, "read_page_ is nullptr (index_iterator operator*)");
  return read_page_->PairAt(index_); 
}

INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator++() -> INDEXITERATOR_TYPE & {
  BUSTUB_ASSERT(read_page_ != nullptr, "read_page_ is nullptr (index_iterator operator++)");
  ++index_;
  if (index_ == read_page_->GetSize()) {
    page_id_t next_page_id = read_page_->GetNextPageId();
#ifdef DEBUG_MOD
      std::cout << "DEBUG: thread " << std::this_thread::get_id() << " Itertatot operator++ [next page_id"
                << next_page_id << "]" << std::endl;
#endif
    // if this page is the last page
    if (next_page_id == INVALID_PAGE_ID) {
      bpm_ = nullptr;
      read_page_ = nullptr;
      read_page_guard_.Drop();
      index_ = -1;
      return *this;
    }
    BUSTUB_ASSERT(bpm_ != nullptr, "bpm_ is nullptr (index_iterator operator++)");
    read_page_guard_ = bpm_->FetchPageRead(next_page_id);
    read_page_ = read_page_guard_.As<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>>();
    index_ = 0;
  }
  return *this;
}

INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator==(const IndexIterator &itr) const -> bool {
  return this->read_page_ == itr.read_page_ && this->index_ == itr.index_;
}

INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator!=(const IndexIterator &itr) const -> bool { return !this->operator==(itr); }

template class IndexIterator<GenericKey<4>, RID, GenericComparator<4>>;

template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>>;

template class IndexIterator<GenericKey<16>, RID, GenericComparator<16>>;

template class IndexIterator<GenericKey<32>, RID, GenericComparator<32>>;

template class IndexIterator<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
