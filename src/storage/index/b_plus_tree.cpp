#include <algorithm>
#include <cstddef>
#include <sstream>
#include <string>

#include "common/config.h"
#include "common/exception.h"
#include "common/logger.h"
#include "common/macros.h"
#include "common/rid.h"
#include "storage/index/b_plus_tree.h"
#include "storage/page/page_guard.h"

namespace bustub {

INDEX_TEMPLATE_ARGUMENTS
BPLUSTREE_TYPE::BPlusTree(std::string name, page_id_t header_page_id, BufferPoolManager *buffer_pool_manager,
                          const KeyComparator &comparator, int leaf_max_size, int internal_max_size)
    : index_name_(std::move(name)),
      bpm_(buffer_pool_manager),
      comparator_(std::move(comparator)),
      leaf_max_size_(leaf_max_size),
      internal_max_size_(internal_max_size),
      header_page_id_(header_page_id) {
  WritePageGuard guard = bpm_->FetchPageWrite(header_page_id_);
  auto root_page = guard.AsMut<BPlusTreeHeaderPage>();
  root_page->root_page_id_ = INVALID_PAGE_ID;
}

/*
 * Helper function to decide whether current b+tree is empty
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::IsEmpty() const -> bool {
  ReadPageGuard guard = bpm_->FetchPageRead(header_page_id_);
  auto root_page = guard.As<BPlusTreeHeaderPage>();
  return root_page->root_page_id_ == INVALID_PAGE_ID;
}
/*****************************************************************************
 * SEARCH
 *****************************************************************************/
/*
 * Return the only value that associated with input key
 * This method is used for point query
 * @return : true means key exists
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetValue(const KeyType &key, std::vector<ValueType> *result, Transaction *txn) -> bool {
#ifdef DEBUG_MOD
  auto log = std::stringstream();
  log << "DEBUG: thread " << std::this_thread::get_id() << " GET request [" << key << "]";
  LOG_DEBUG("%s", log.str().c_str());
#endif
  // Declaration of context instance.
  Context ctx;
  // Get root page id
  ReadPageGuard header_guard = bpm_->FetchPageRead(header_page_id_);
  auto header_page = header_guard.As<BPlusTreeHeaderPage>();
  page_id_t page_id_to_fetch = header_page->root_page_id_;
  ctx.read_set_.emplace_back(std::move(header_guard));
  do {
    // If page_id_to_fetch is invalid, return false
    if (page_id_to_fetch == INVALID_PAGE_ID) {
      return false;
    }
    // Fetch page and Unlock old pages
    ReadPageGuard guard = bpm_->FetchPageRead(page_id_to_fetch);
    while (!ctx.read_set_.empty()) {
      ctx.read_set_.pop_front();
    }
    // If page is a leaf page
    auto page = guard.As<BPlusTreePage>();
    if (page->IsLeafPage()) {
      auto leaf_page = guard.As<LeafPage>();
      bool flag = false;
      // FIXME: use binary search
      int index = 0;
      while (index < leaf_page->GetSize() && comparator_(leaf_page->KeyAt(index), key) < 0) {
        ++index;
      }
      while (index < leaf_page->GetSize() && comparator_(leaf_page->KeyAt(index), key) == 0) {
        result->push_back(leaf_page->ValueAt(index));
        ++index;
        flag = true;
      }
      return flag;
    }
    // Else if page is an internal page
    auto internal_page = guard.As<InternalPage>();
    // find the first key greater than *key*
    int index = UpperBound(internal_page, key);
    page_id_to_fetch = internal_page->ValueAt(index - 1);
    // Add this page to read_set
    ctx.read_set_.emplace_back(std::move(guard));
  } while (true);
  return false;
}

/*****************************************************************************
 * INSERTION
 *****************************************************************************/
/*
 * Insert constant key & value pair into b+ tree
 * if current tree is empty, start new tree, update root page id and insert
 * entry, otherwise insert into leaf page.
 * @return: since we only support unique key, if user try to insert duplicate
 * keys return false, otherwise return true.
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Insert(const KeyType &key, const ValueType &value, Transaction *txn) -> bool {
#ifdef DEBUG_MOD
  auto log = std::stringstream();
  log << "DEBUG: thread " << std::this_thread::get_id() << " INSERT request [" << key << "]";
  LOG_DEBUG("%s", log.str().c_str());
#endif
  bool rt = false;
  {
    // Declaration of context instance.
    Context ctx;
    // Get root page id
    WritePageGuard header_guard = bpm_->FetchPageWrite(header_page_id_);
    auto header_page = header_guard.As<BPlusTreeHeaderPage>();

    if (header_page->root_page_id_ == INVALID_PAGE_ID) {
      // If tree is empty, create a new tree
      auto header_page_mut = header_guard.AsMut<BPlusTreeHeaderPage>();
      auto root_guard = bpm_->NewPageGuarded(&header_page_mut->root_page_id_);
      BUSTUB_ASSERT(root_guard.GetData() != nullptr, "allocate new page error");
      auto leaf_page = root_guard.AsMut<LeafPage>();
      leaf_page->Init(leaf_max_size_);
      leaf_page->InsertAt(key, value, 0);
      rt = true;
    } else {
      // If the tree is not empty, insert
      ctx.write_set_.emplace_back(std::move(header_guard));
      ctx.root_page_id_ = header_page->root_page_id_;
      rt = InsertRecursively(header_page->root_page_id_, ctx, key, value);
    }
  }
// #ifdef DEBUG_MOD
//   LOG_DEBUG("%s", DrawBPlusTree().c_str());
// #endif
  return rt;
}

/*****************************************************************************
 * REMOVE
 *****************************************************************************/
/*
 * Delete key & value pair associated with input key
 * If current tree is empty, return immediately.
 * If not, User needs to first find the right leaf page as deletion target, then
 * delete entry from leaf page. Remember to deal with redistribute or merge if
 * necessary.
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Remove(const KeyType &key, Transaction *txn) {
#ifdef DEBUG_MOD
  auto log = std::stringstream();
  log << "DEBUG: thread " << std::this_thread::get_id() << " Delete request [" << key << "]";
  LOG_DEBUG("%s", log.str().c_str());
#endif
  {
    // Declaration of context instance.
    Context ctx;
    (void)ctx;
    // Get root page id
    WritePageGuard header_guard = bpm_->FetchPageWrite(header_page_id_);
    auto header_page = header_guard.AsMut<BPlusTreeHeaderPage>();
    if (header_page->root_page_id_ == INVALID_PAGE_ID) {
      return;
    }
    ctx.write_set_.emplace_back(std::move(header_guard));
    ctx.root_page_id_ = header_page->root_page_id_;
    RemoveRecursively(header_page->root_page_id_, ctx, key);
  }
// #ifdef DEBUG_MOD
//   LOG_DEBUG("%s", DrawBPlusTree().c_str());
// #endif
}

/*****************************************************************************
 * INDEX ITERATOR
 *****************************************************************************/
/*
 * Input parameter is void, find the leftmost leaf page first, then construct
 * index iterator
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin() -> INDEXITERATOR_TYPE {
#ifdef DEBUG_MOD
  auto log = std::stringstream();
  log << "DEBUG: thread " << std::this_thread::get_id() << " Begin request";
  LOG_DEBUG("%s", log.str().c_str());
#endif
  // Declaration of context instance.
  Context ctx;
  // Get root page id
  ReadPageGuard header_guard = bpm_->FetchPageRead(header_page_id_);
  auto header_page = header_guard.As<BPlusTreeHeaderPage>();
  page_id_t page_id_to_fetch = header_page->root_page_id_;
  // If tree is empty, return empty iterator
  if (page_id_to_fetch == INVALID_PAGE_ID) {
    return {nullptr, ReadPageGuard{}, -1};
  }
  ctx.read_set_.emplace_back(std::move(header_guard));
  do {
    BUSTUB_ASSERT(page_id_to_fetch != INVALID_PAGE_ID, "tree is empty");
    // Fetch page and Unlock old pages
    ReadPageGuard guard = bpm_->FetchPageRead(page_id_to_fetch);
    while (!ctx.read_set_.empty()) {
      ctx.read_set_.pop_front();
    }
    // If page is a leaf page
    auto page = guard.As<BPlusTreePage>();
    if (page->IsLeafPage()) {
      return INDEXITERATOR_TYPE(bpm_, std::move(guard), 0);
    }
    // Else if page is an internal page
    auto internal_page = guard.As<InternalPage>();
    // find the first key greater than *key*
    page_id_to_fetch = internal_page->ValueAt(0);
    // Add this page to read_set
    ctx.read_set_.emplace_back(std::move(guard));
  } while (true);
}

/*
 * Input parameter is low key, find the leaf page that contains the input key
 * first, then construct index iterator
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin(const KeyType &key) -> INDEXITERATOR_TYPE {
#ifdef DEBUG_MOD
  auto log = std::stringstream();
  log << "DEBUG: thread " << std::this_thread::get_id() << " Begin request [" << key << "]";
  LOG_DEBUG("%s", log.str().c_str());
#endif
  // Declaration of context instance.
  Context ctx;
  // Get root page id
  ReadPageGuard header_guard = bpm_->FetchPageRead(header_page_id_);
  auto header_page = header_guard.As<BPlusTreeHeaderPage>();
  page_id_t page_id_to_fetch = header_page->root_page_id_;
  // If tree is empty, return empty iterator
  if (page_id_to_fetch == INVALID_PAGE_ID) {
    return {nullptr, ReadPageGuard{}, -1};
  }
  ctx.read_set_.emplace_back(std::move(header_guard));
  do {
    // If page_id_to_fetch is invalid, return false
    BUSTUB_ASSERT(page_id_to_fetch != INVALID_PAGE_ID, "tree is empty");
    // Fetch page and Unlock old pages
    ReadPageGuard guard = bpm_->FetchPageRead(page_id_to_fetch);
    while (!ctx.read_set_.empty()) {
      ctx.read_set_.pop_front();
    }
    // If page is a leaf page
    auto page = guard.As<BPlusTreePage>();
    if (page->IsLeafPage()) {
      auto leaf_page = guard.As<LeafPage>();
      // FIXME: use binary search
      int index = 0;
      while (index < leaf_page->GetSize() && comparator_(leaf_page->KeyAt(index), key) < 0) {
        ++index;
      }
      // if key is greater than all keys in this page
      if (index == leaf_page->GetSize()) {
        // if this is the last page, return empty iterator
        if (leaf_page->GetNextPageId() == INVALID_PAGE_ID) {
          return {nullptr, ReadPageGuard{}, -1};
        }
        // return the first key of next page
        ReadPageGuard rst_guard = bpm_->FetchPageRead(leaf_page->GetNextPageId());
        return {bpm_, std::move(rst_guard), 0};
      } 
      // if key is in this page, return the iterator
      return {bpm_, std::move(guard), index};
    }
    // Else if page is an internal page
    auto internal_page = guard.As<InternalPage>();
    // find the first key greater than *key*
    int index = UpperBound(internal_page, key);
    page_id_to_fetch = internal_page->ValueAt(index - 1);
    // Add this page to read_set
    ctx.read_set_.emplace_back(std::move(guard));
  } while (true);
}

/*
 * Input parameter is void, construct an index iterator representing the end
 * of the key/value pair in the leaf node
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::End() -> INDEXITERATOR_TYPE {
#ifdef DEBUG_MOD
  auto log = std::stringstream();
  log << "DEBUG: thread " << std::this_thread::get_id() << " End request";
  LOG_DEBUG("%s", log.str().c_str());
#endif
  // return empty iterator
  return {nullptr, ReadPageGuard{}, -1};
  // // Declaration of context instance.
  // Context ctx;
  // // Get root page id
  // ReadPageGuard header_guard = bpm_->FetchPageRead(header_page_id_);
  // auto header_page = header_guard.As<BPlusTreeHeaderPage>();
  // page_id_t page_id_to_fetch = header_page->root_page_id_;
  // ctx.read_set_.emplace_back(std::move(header_guard));
  // do {
  //   // If page_id_to_fetch is invalid, return false
  //   BUSTUB_ASSERT(page_id_to_fetch != INVALID_PAGE_ID, "tree is empty");
  //   // Fetch page and Unlock old pages
  //   ReadPageGuard guard = bpm_->FetchPageRead(page_id_to_fetch);
  //   while (!ctx.read_set_.empty()) {
  //     ctx.read_set_.pop_front();
  //   }
  //   // If page is a leaf page
  //   auto page = guard.As<BPlusTreePage>();
  //   if (page->IsLeafPage()) {
  //     return INDEXITERATOR_TYPE(bpm_, std::move(guard), page->GetSize());
  //   }
  //   // Else if page is an internal page
  //   auto internal_page = guard.As<InternalPage>();
  //   // find the first key greater than *key*
  //   page_id_to_fetch = internal_page->ValueAt(internal_page->GetSize() - 1);
  //   // Add this page to read_set
  //   ctx.read_set_.emplace_back(std::move(guard));
  // } while (true);
}

/**
 * @return Page id of the root of this tree
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetRootPageId() -> page_id_t {
#ifdef DEBUG_MOD
  auto log = std::stringstream();
  log << "DEBUG: thread " << std::this_thread::get_id() << " GET_ROOT_PAGEID request";
  LOG_DEBUG("%s", log.str().c_str());
#endif
  ReadPageGuard header_guard = bpm_->FetchPageRead(header_page_id_);
  auto header_page = header_guard.As<BPlusTreeHeaderPage>();
  return header_page->root_page_id_;
}

/*****************************************************************************
 * UTILITIES AND DEBUG
 *****************************************************************************/

/*
 * This method is used for test only
 * Read data from file and insert one by one
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::InsertFromFile(const std::string &file_name, Transaction *txn) {
  int64_t key;
  std::ifstream input(file_name);
  while (input >> key) {
    KeyType index_key;
    index_key.SetFromInteger(key);
    RID rid(key);
    Insert(index_key, rid, txn);
  }
}
/*
 * This method is used for test only
 * Read data from file and remove one by one
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::RemoveFromFile(const std::string &file_name, Transaction *txn) {
  int64_t key;
  std::ifstream input(file_name);
  while (input >> key) {
    KeyType index_key;
    index_key.SetFromInteger(key);
    Remove(index_key, txn);
  }
}

/*
 * This method is used for test only
 * Read data from file and insert/remove one by one
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::BatchOpsFromFile(const std::string &file_name, Transaction *txn) {
  int64_t key;
  char instruction;
  std::ifstream input(file_name);
  while (input) {
    input >> instruction >> key;
    RID rid(key);
    KeyType index_key;
    index_key.SetFromInteger(key);
    switch (instruction) {
      case 'i':
        Insert(index_key, rid, txn);
        break;
      case 'd':
        Remove(index_key, txn);
        break;
      default:
        break;
    }
  }
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Print(BufferPoolManager *bpm) {
  auto root_page_id = GetRootPageId();
  auto guard = bpm->FetchPageBasic(root_page_id);
  PrintTree(guard.PageId(), guard.template As<BPlusTreePage>());
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::PrintTree(page_id_t page_id, const BPlusTreePage *page) {
  if (page->IsLeafPage()) {
    auto *leaf = reinterpret_cast<const LeafPage *>(page);
    std::cout << "Leaf Page: " << page_id << "\tNext: " << leaf->GetNextPageId() << std::endl;

    // Print the contents of the leaf page.
    std::cout << "Contents: ";
    for (int i = 0; i < leaf->GetSize(); i++) {
      std::cout << leaf->KeyAt(i);
      if ((i + 1) < leaf->GetSize()) {
        std::cout << ", ";
      }
    }
    std::cout << std::endl;
    std::cout << std::endl;

  } else {
    auto *internal = reinterpret_cast<const InternalPage *>(page);
    std::cout << "Internal Page: " << page_id << std::endl;

    // Print the contents of the internal page.
    std::cout << "Contents: ";
    for (int i = 0; i < internal->GetSize(); i++) {
      std::cout << internal->KeyAt(i) << ": " << internal->ValueAt(i);
      if ((i + 1) < internal->GetSize()) {
        std::cout << ", ";
      }
    }
    std::cout << std::endl;
    std::cout << std::endl;
    for (int i = 0; i < internal->GetSize(); i++) {
      auto guard = bpm_->FetchPageBasic(internal->ValueAt(i));
      PrintTree(guard.PageId(), guard.template As<BPlusTreePage>());
    }
  }
}

/**
 * This method is used for debug only, You don't need to modify
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Draw(BufferPoolManager *bpm, const std::string &outf) {
  if (IsEmpty()) {
    LOG_WARN("Drawing an empty tree");
    return;
  }

  std::ofstream out(outf);
  out << "digraph G {" << std::endl;
  auto root_page_id = GetRootPageId();
  auto guard = bpm->FetchPageBasic(root_page_id);
  ToGraph(guard.PageId(), guard.template As<BPlusTreePage>(), out);
  out << "}" << std::endl;
  out.close();
}

/**
 * This method is used for debug only, You don't need to modify
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::ToGraph(page_id_t page_id, const BPlusTreePage *page, std::ofstream &out) {
  std::string leaf_prefix("LEAF_");
  std::string internal_prefix("INT_");
  if (page->IsLeafPage()) {
    auto *leaf = reinterpret_cast<const LeafPage *>(page);
    // Print node name
    out << leaf_prefix << page_id;
    // Print node properties
    out << "[shape=plain color=green ";
    // Print data of the node
    out << "label=<<TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"4\">\n";
    // Print data
    out << "<TR><TD COLSPAN=\"" << leaf->GetSize() << "\">P=" << page_id << "</TD></TR>\n";
    out << "<TR><TD COLSPAN=\"" << leaf->GetSize() << "\">"
        << "max_size=" << leaf->GetMaxSize() << ",min_size=" << leaf->GetMinSize() << ",size=" << leaf->GetSize()
        << "</TD></TR>\n";
    out << "<TR>";
    for (int i = 0; i < leaf->GetSize(); i++) {
      out << "<TD>" << leaf->KeyAt(i) << "</TD>\n";
    }
    out << "</TR>";
    // Print table end
    out << "</TABLE>>];\n";
    // Print Leaf node link if there is a next page
    if (leaf->GetNextPageId() != INVALID_PAGE_ID) {
      out << leaf_prefix << page_id << " -> " << leaf_prefix << leaf->GetNextPageId() << ";\n";
      out << "{rank=same " << leaf_prefix << page_id << " " << leaf_prefix << leaf->GetNextPageId() << "};\n";
    }
  } else {
    auto *inner = reinterpret_cast<const InternalPage *>(page);
    // Print node name
    out << internal_prefix << page_id;
    // Print node properties
    out << "[shape=plain color=pink ";  // why not?
    // Print data of the node
    out << "label=<<TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"4\">\n";
    // Print data
    out << "<TR><TD COLSPAN=\"" << inner->GetSize() << "\">P=" << page_id << "</TD></TR>\n";
    out << "<TR><TD COLSPAN=\"" << inner->GetSize() << "\">"
        << "max_size=" << inner->GetMaxSize() << ",min_size=" << inner->GetMinSize() << ",size=" << inner->GetSize()
        << "</TD></TR>\n";
    out << "<TR>";
    for (int i = 0; i < inner->GetSize(); i++) {
      out << "<TD PORT=\"p" << inner->ValueAt(i) << "\">";
      if (i > 0) {
        out << inner->KeyAt(i);
      } else {
        out << " ";
      }
      out << "</TD>\n";
    }
    out << "</TR>";
    // Print table end
    out << "</TABLE>>];\n";
    // Print leaves
    for (int i = 0; i < inner->GetSize(); i++) {
      auto child_guard = bpm_->FetchPageBasic(inner->ValueAt(i));
      auto child_page = child_guard.template As<BPlusTreePage>();
      ToGraph(child_guard.PageId(), child_page, out);
      if (i > 0) {
        auto sibling_guard = bpm_->FetchPageBasic(inner->ValueAt(i - 1));
        auto sibling_page = sibling_guard.template As<BPlusTreePage>();
        if (!sibling_page->IsLeafPage() && !child_page->IsLeafPage()) {
          out << "{rank=same " << internal_prefix << sibling_guard.PageId() << " " << internal_prefix
              << child_guard.PageId() << "};\n";
        }
      }
      out << internal_prefix << page_id << ":p" << child_guard.PageId() << " -> ";
      if (child_page->IsLeafPage()) {
        out << leaf_prefix << child_guard.PageId() << ";\n";
      } else {
        out << internal_prefix << child_guard.PageId() << ";\n";
      }
    }
  }
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::DrawBPlusTree() -> std::string {
  if (IsEmpty()) {
    return "()";
  }

  PrintableBPlusTree p_root = ToPrintableBPlusTree(GetRootPageId());
  std::ostringstream out_buf;
  p_root.Print(out_buf);

  return out_buf.str();
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::ToPrintableBPlusTree(page_id_t root_id) -> PrintableBPlusTree {
  auto root_page_guard = bpm_->FetchPageBasic(root_id);
  auto root_page = root_page_guard.template As<BPlusTreePage>();
  PrintableBPlusTree proot;

  if (root_page->IsLeafPage()) {
    auto leaf_page = root_page_guard.template As<LeafPage>();
    proot.keys_ = leaf_page->ToString();
    proot.size_ = proot.keys_.size() + 4;  // 4 more spaces for indent

    return proot;
  }

  // draw internal page
  auto internal_page = root_page_guard.template As<InternalPage>();
  proot.keys_ = internal_page->ToString();
  proot.size_ = 0;
  for (int i = 0; i < internal_page->GetSize(); i++) {
    page_id_t child_id = internal_page->ValueAt(i);
    PrintableBPlusTree child_node = ToPrintableBPlusTree(child_id);
    proot.size_ += child_node.size_;
    proot.children_.push_back(child_node);
  }

  return proot;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::UpperBound(const LeafPage *page, const KeyType &key) -> size_t {
  int index = 0;
  while (index < page->GetSize() && comparator_(page->KeyAt(index), key) <= 0) {
    ++index;
  }
  return index;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::UpperBound(const InternalPage *page, const KeyType &key) -> size_t {
  int index = 1;
  while (index < page->GetSize() && comparator_(page->KeyAt(index), key) <= 0) {
    ++index;
  }
  return index;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::InsertRecursively(page_id_t page_id_to_fetch, Context &ctx, const KeyType &key,
                                       const ValueType &value, Transaction *txn) -> bool {
  BUSTUB_ASSERT(page_id_to_fetch != INVALID_PAGE_ID, "Insert error, invalid page");
  // fetch cur_page
  WritePageGuard cur_guard = bpm_->FetchPageWrite(page_id_to_fetch);
  auto cur_page = cur_guard.As<BPlusTreePage>();

  // if header_page is not going to split, release locked pages before
  if (cur_page->GetSize() < cur_page->GetMaxSize()) {
    while (!ctx.write_set_.empty()) {
      ctx.write_set_.pop_front();
    }
  }

  // if is internal_page, walk through
  if (!cur_page->IsLeafPage()) {
    auto internal_page = cur_guard.As<InternalPage>();
    int index = UpperBound(internal_page, key) - 1;
    ctx.write_set_.emplace_back(std::move(cur_guard));
    return InsertRecursively(internal_page->ValueAt(index), ctx, key, value);
  }

  // if cur_page is leaf_page, insert
  auto cur_leaf_page = cur_guard.AsMut<LeafPage>();
  int index = UpperBound(cur_leaf_page, key);
  if (comparator_(cur_leaf_page->KeyAt(index - 1), key) == 0) {
    return false;
  }
  // if not full, insert and return
  if (cur_leaf_page->GetSize() < cur_leaf_page->GetMaxSize()) {
    cur_leaf_page->InsertAt(key, value, index);
    return true;
  }
  // if full, split, fixme
  page_id_t new_split_page_id = INVALID_PAGE_ID;
  auto new_split_page_guard = bpm_->NewPageGuarded(&new_split_page_id);
  BUSTUB_ASSERT(new_split_page_id != INVALID_PAGE_ID, "allocate error at split");
  auto new_leaf_page = new_split_page_guard.AsMut<LeafPage>();
  new_leaf_page->Init(leaf_max_size_);
  cur_leaf_page->InsertAtAndSplitTo(key, value, index, new_leaf_page);
  new_leaf_page->SetNextPageId(cur_leaf_page->GetNextPageId());
  cur_leaf_page->SetNextPageId(new_split_page_id);

  // if cur page is root page, update root
  if (ctx.IsRootPage(page_id_to_fetch)) {
    page_id_t new_root_page_id = INVALID_PAGE_ID;
    auto new_root_guard = bpm_->NewPageGuarded(&new_root_page_id);
    BUSTUB_ASSERT(new_root_page_id != INVALID_PAGE_ID, "allocate error at split");
    auto new_root_page = new_root_guard.AsMut<InternalPage>();
    new_root_page->Init(internal_max_size_);
    new_root_page->InsertAt(cur_leaf_page->KeyAt(0), page_id_to_fetch, 0);
    new_root_page->InsertAt(new_leaf_page->KeyAt(0), new_split_page_id, 1);
    auto &header_page_guard = ctx.write_set_.back();
    header_page_guard.AsMut<BPlusTreeHeaderPage>()->root_page_id_ = new_root_page_id;
    return true;
  }

  // if cur page is not root, insert into father
  return InsertIntoParent(ctx, new_leaf_page->KeyAt(0), new_split_page_id);
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::InsertIntoParent(Context &ctx, const KeyType &key, const page_id_t &value, Transaction *txn)
    -> bool {
  BUSTUB_ASSERT(!ctx.write_set_.empty(), "InsertIntoParent error, write_set is empty");
  // get cur page
  auto cur_guard = std::move(ctx.write_set_.back());
  ctx.write_set_.pop_back();
  auto cur_internal_page = cur_guard.AsMut<InternalPage>();

  // if not full, insert and return
  if (cur_internal_page->GetSize() < cur_internal_page->GetMaxSize()) {
    int index = UpperBound(cur_internal_page, key);
    cur_internal_page->InsertAt(key, value, index);
    return true;
  }

  // if full, split
  page_id_t new_split_page_id = INVALID_PAGE_ID;
  auto new_split_page_guard = bpm_->NewPageGuarded(&new_split_page_id);
  BUSTUB_ASSERT(new_split_page_id != INVALID_PAGE_ID, "allocate error at split");
  auto new_internal_page = new_split_page_guard.AsMut<InternalPage>();
  new_internal_page->Init(internal_max_size_);
  int index = UpperBound(cur_internal_page, key);
  cur_internal_page->InsertAtAndSplitTo(key, value, index, new_internal_page);

  // if root, update root
  if (ctx.IsRootPage(cur_guard.PageId())) {
    page_id_t new_root_page_id = INVALID_PAGE_ID;
    auto new_root_guard = bpm_->NewPageGuarded(&new_root_page_id);
    BUSTUB_ASSERT(new_root_page_id != INVALID_PAGE_ID, "allocate error at split");
    auto new_root_page = new_root_guard.AsMut<InternalPage>();
    new_root_page->Init(internal_max_size_);
    new_root_page->InsertAt(cur_internal_page->KeyAt(0), cur_guard.PageId(), 0);
    new_root_page->InsertAt(new_internal_page->KeyAt(0), new_split_page_id, 1);
    auto &header_page_guard = ctx.write_set_.back();
    header_page_guard.AsMut<BPlusTreeHeaderPage>()->root_page_id_ = new_root_page_id;
    return true;
  }

  // if not root, update father
  return InsertIntoParent(ctx, new_internal_page->KeyAt(0), new_split_page_id);
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::RemoveRecursively(page_id_t page_id_to_fetch, Context &ctx, const KeyType &key, Transaction *txn) {
  BUSTUB_ASSERT(page_id_to_fetch != INVALID_PAGE_ID, "invalid page id to fetch");
  // fetch cur page
  WritePageGuard cur_guard = bpm_->FetchPageWrite(page_id_to_fetch);
  auto cur_page = cur_guard.As<BPlusTreePage>();

  // if page is internal page, walk through
  if (!cur_page->IsLeafPage()) {
    // if page size is bigger than min_size, release locks
    if ((ctx.IsRootPage(page_id_to_fetch) && cur_page->GetSize() > 2) || cur_page->GetSize() > cur_page->GetMinSize()) {
      while (!ctx.write_set_.empty()) {
        ctx.write_set_.pop_front();
      }
    }
    // walk through
    auto cur_page_internal = cur_guard.As<InternalPage>();
    int index = UpperBound(cur_page_internal, key) - 1;
    ctx.write_set_.emplace_back(std::move(cur_guard));
    RemoveRecursively(cur_page_internal->ValueAt(index), ctx, key);
    return;
  }

  // if page is leaf page
  // if page size is bigger than min_size, release locks
  if ((ctx.IsRootPage(page_id_to_fetch) && cur_page->GetSize() > 1) || cur_page->GetSize() > cur_page->GetMinSize()) {
    while (!ctx.write_set_.empty()) {
      ctx.write_set_.pop_front();
    }
  }

  // remove key/value pair from leaf page
  auto cur_leaf_page = cur_guard.AsMut<LeafPage>();
  int index = UpperBound(cur_leaf_page, key) - 1;
  if (comparator_(cur_leaf_page->KeyAt(index), key) != 0) {
    return;
  }
  cur_leaf_page->RemoveAt(index);

  // if root page, just check if the tree is empty
  if (ctx.IsRootPage(page_id_to_fetch)) {
    if (cur_leaf_page->GetSize() <= 0) {
      auto &header_guard = ctx.write_set_.back();
      header_guard.AsMut<BPlusTreeHeaderPage>()->root_page_id_ = INVALID_PAGE_ID;
      cur_guard.Drop();
      bpm_->DeletePage(page_id_to_fetch);
    }
    return;
  }

  // if not root page, check for size, if no need to borrow/merge, just return
  if (cur_leaf_page->GetSize() >= cur_leaf_page->GetMinSize()) {
    return;
  }

  // need to borrow / merge
  // fetch parent
  auto &parent_guard = ctx.write_set_.back();
  auto parent_internal_page = parent_guard.AsMut<InternalPage>();
  index = UpperBound(parent_internal_page, cur_leaf_page->KeyAt(0)) - 1;

  if (index > 0) {
    // left brother exists
    int left_index = index - 1;
    page_id_t page_id_left = parent_internal_page->ValueAt(left_index);
    auto left_guard = bpm_->FetchPageWrite(page_id_left);
    auto left_leaf_page = left_guard.AsMut<LeafPage>();
    // borrow from left
    if (cur_leaf_page->BorrowFromLeft(left_leaf_page)) {
      parent_internal_page->SetKeyAt(cur_leaf_page->KeyAt(0), index);
      return;
    }
    // merge to left
    cur_leaf_page->MergeToLeft(left_leaf_page);
    left_leaf_page->SetNextPageId(cur_leaf_page->GetNextPageId());
    // delete cur page
    page_id_t page_id_to_delete = cur_guard.PageId();
    cur_guard.Drop();
    bpm_->DeletePage(page_id_to_delete);
    // update father page
    RemoveFromParent(ctx, parent_internal_page->KeyAt(index));
  } else {
    // right brother exists
    int right_index = index + 1;
    page_id_t page_id_right = parent_internal_page->ValueAt(right_index);
    auto right_guard = bpm_->FetchPageWrite(page_id_right);
    auto right_leaf_page = right_guard.AsMut<LeafPage>();
    // borrow from right
    if (cur_leaf_page->BorrowFromRight(right_leaf_page)) {
      parent_internal_page->SetKeyAt(right_leaf_page->KeyAt(0), right_index);
      return;
    }
    // merge to right(the same as right page merge to left)
    right_leaf_page->MergeToLeft(cur_leaf_page);
    cur_leaf_page->SetNextPageId(right_leaf_page->GetNextPageId());
    // delete right page
    page_id_t page_id_to_delete = right_guard.PageId();
    right_guard.Drop();
    bpm_->DeletePage(page_id_to_delete);
    // update father page
    RemoveFromParent(ctx, parent_internal_page->KeyAt(right_index));
  }
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::RemoveFromParent(Context &ctx, const KeyType &key, Transaction *txn) {
  BUSTUB_ASSERT(!ctx.write_set_.empty(), "RemoveFromParent error, write_set is empty");
  // get cur page
  auto cur_guard = std::move(ctx.write_set_.back());
  ctx.write_set_.pop_back();
  auto cur_internal_page = cur_guard.AsMut<InternalPage>();

  // remove
  int index = UpperBound(cur_internal_page, key) - 1;
  cur_internal_page->RemoveAt(index);

  // if root, check if update header page
  if (ctx.IsRootPage(cur_guard.PageId())) {
    if (cur_internal_page->GetSize() < 2) {
      auto &header_guard = ctx.write_set_.back();
      header_guard.AsMut<BPlusTreeHeaderPage>()->root_page_id_ = cur_internal_page->ValueAt(0);
      cur_guard.Drop();
      bpm_->DeletePage(cur_guard.PageId());
    }
    return;
  }

  // if not root page, check for size, if no need to borrow/merge, just return
  if (cur_internal_page->GetSize() >= cur_internal_page->GetMinSize()) {
    return;
  }

  // need to borrow / merge
  auto &parent_guard = ctx.write_set_.back();
  auto parent_internal_page = parent_guard.AsMut<InternalPage>();
  index = UpperBound(parent_internal_page, cur_internal_page->KeyAt(1)) - 1;
  if (index > 0) {
    // left brother exists
    int left_index = index - 1;
    page_id_t page_id_left = parent_internal_page->ValueAt(left_index);
    auto left_guard = bpm_->FetchPageWrite(page_id_left);
    auto left_internal_page = left_guard.AsMut<InternalPage>();
    // borrow from left
    if (cur_internal_page->BorrowFromLeft(left_internal_page)) {
      cur_internal_page->SetKeyAt(parent_internal_page->KeyAt(index), 1);
      parent_internal_page->SetKeyAt(cur_internal_page->KeyAt(0), index);
      return;
    }
    // merge to left
    cur_internal_page->SetKeyAt(parent_internal_page->KeyAt(index), 0);
    cur_internal_page->MergeToLeft(left_internal_page);
    // delete cur page
    page_id_t page_id_to_delete = cur_guard.PageId();
    cur_guard.Drop();
    bpm_->DeletePage(page_id_to_delete);
    // update father page
    RemoveFromParent(ctx, parent_internal_page->KeyAt(index));
  } else {
    // right brother exists
    int right_index = index + 1;
    page_id_t page_id_right = parent_internal_page->ValueAt(right_index);
    auto right_guard = bpm_->FetchPageWrite(page_id_right);
    auto right_internal_page = right_guard.AsMut<InternalPage>();
    // borrow from right
    if (cur_internal_page->BorrowFromRight(right_internal_page)) {
      cur_internal_page->SetKeyAt(parent_internal_page->KeyAt(right_index), cur_internal_page->GetSize() - 1);
      parent_internal_page->SetKeyAt(right_internal_page->KeyAt(0), right_index);
      return;
    }
    // merge to right (the same as right page merge to left)
    right_internal_page->SetKeyAt(parent_internal_page->KeyAt(right_index), 0);
    right_internal_page->MergeToLeft(cur_internal_page);
    // delete right page
    page_id_t page_id_to_delete = right_guard.PageId();
    right_guard.Drop();
    bpm_->DeletePage(page_id_to_delete);
    // update father page
    RemoveFromParent(ctx, parent_internal_page->KeyAt(right_index));
  }
}

template class BPlusTree<GenericKey<4>, RID, GenericComparator<4>>;

template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>>;

template class BPlusTree<GenericKey<16>, RID, GenericComparator<16>>;

template class BPlusTree<GenericKey<32>, RID, GenericComparator<32>>;

template class BPlusTree<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
