# Project 1 BufferPool Manager
## 1.1 数据结构
```C++
class BufferPoolManager {
  /* 页缓存池，BufferPool从磁盘上以页为单位拷入内存 */
  Page *pages_;
  /* 记录page_id到frame_id的映射，即page被拷贝到页缓存池的哪个frame */
  std::unordered_map<page_id_t, frame_id_t> page_table_;
  /* LRU_K淘汰算法组件，记录每个frame的最近k次访问时间戳以及pinned状态，在空闲frame不足时用来选举将要牺牲的frame */
  std::unique_ptr<LRUKReplacer> replacer_;
  /* 以链表形式记录空闲的frame */
  std::list<frame_id_t> free_list_;
}

class Page {
  /* page中保存的数据，大小为4KB */
  char *data_;
  /* 该page的page_id */
  page_id_t page_id_ = INVALID_PAGE_ID;
  /* 引用计数，只有在从BufferPool中FetchPage时才会增加，
   * 使用page_guard管理资源，在PageGuard析构时调用UnpinPage减少引用计数(引用计数为0时解除frame的锁定以允许被牺牲)（RAII）*/
  int pin_count_ = 0;
  /* 脏位，当对page进行过写操作时会标记为dirty，在该page被移除bufferpool时要写回磁盘 */
  bool is_dirty_ = false;
  /* 读写锁，一个页可以同时多个读，一个写 */
  ReaderWriterLatch rwlatch_;
}

```
## 1.2 优化
### Problem 1: 磁盘I/O
1. 问题：qps中占用较大的一项是磁盘I/O，因此当读取或写回磁盘页时，如果加整个BufferPool的大锁，会增加锁竞争的时间。
2. 优化思路：可以使用请求队列使得写请求变为异步执行，读请求仍需要保持同步(使用C++ promise future)。为每个frame创建一个请求队列，并在后台开启一个线程，以FIFO模式处理读写请求，写请求在创建后会立即返回，进而被异步执行，读请求则会因为future.get()陷入阻塞，直到后台线程执行promise.set()之后才会被唤醒。
### Problem 2：Scan请求会扰乱Get请求
1. 问题：Scan请求通常会大量访问磁盘页且只访问一次，导致Get请求暂时未达到k次的缓存页被刷新掉
2. 优化思路：使用LFU淘汰未达到k次访问的frame（由于Get请求通常访问多次，而Scan请求仅访问一次）；使用两个队列分别保存Scan请求和Get请求未达到k次访问的frame，优先淘汰Scan请求队列中的frame

# Project 2 Concurrent BPlusTree Index
## 2.1 数据结构
```C++
/* 
 * Header format (size in byte, 12 bytes in total):
 * | PageType (4) | CurrentSize (4) | MaxSize (4) |
 */
class BPlusTreePage;

/*
 * Internal page format (keys are stored in increasing order):
 * | HEADER | KEY(1)+PAGE_ID(1) | KEY(2)+PAGE_ID(2) | ... | KEY(n)+PAGE_ID(n) |
 */
class BPlusTreeInternalPage : public BPlusTreePage;

/*
 * Leaf page format (keys are stored in order):
 * | HEADER | NEXT_PAGE_ID | KEY(1) + RID(1) | KEY(2) + RID(2) | ... | KEY(n) + RID(n)
 * RID = PAGE_ID + RECORD_INDEX
 */
class BPlusTreeLeafPage : public BPlusTreePage;

class BPlusTree {
  /* BufferPool Manager */
  BufferPoolManager *bpm_;
  /* 额外页，记录BPlusTree根节点页的page_id，如果树为空，则root_page_id = INVALID_PAGE_ID */
  page_id_t header_page_id_;
};
```
## 2.2 BPlusTree基本思路
1. 分为InternalPage和LeafPage，非聚簇索引中LeafPage的value记录的是record_id，聚簇索引则记录record的值
2. concurrent由螃蟹规则实现，由根节点开始一路向下上锁，在确认当前节点没有分裂可能的时候，释放父节点以及往上的锁
3. 上锁时需要保证从上至下，从左至右，不然会发生死锁（许多数据库管理系统在进行scan操作时，即在叶子节点扫描时只能从左至右或从右至左，无法兼得就是这个原因）

## 2.3 优化
### Problem 1: 优化读多写少的场景
1. 问题：由于每个leaf_page其实很大，因此在写操作时，没必要每次都从上往下加一遍写锁，写锁会严重阻塞其他操作
2. 优化思路：使用乐观锁，即假设写操作不会导致叶节点分裂，从根节点开始一路向下加读锁，到叶子节点时加写锁，此时检测插入一个键值对后是否会使叶子节点分裂，如果不会则插入完成；如果会导致分裂，则从根节点开始重新加写锁执行一遍插入操作


# Project 3 Executors
## 3.1 Access Method Executors
1. SeqScan：获取表的table_iterator，上层调用一次next，返回一次数据并让iterator++
2. Insert：在基表上一次性插入完数据，并添加有关索引（记录key和RID），在上层调用next时返回插入结果
3. Delete：每次仅删除元数据以及对应索引键
4. IndexScan：根据索引查找到key的RID，然后用RID去原表获得数据
## 3.2 Join Executors
1. NestedLoopJoin: 将内表的数据读到内存里，然后每次调用外表的next，与内表所有数据匹配；（可能遇到表太大导致内存不足的情况，需要实现BlockNestedLoopJoin，即读取内外表各一个块，然后匹配）
2. HashJoin：对外表根据Join On的key建立哈希表，value是一个值的数组，遍历内表，对每一条记录进行哈希匹配（可使用布隆过滤器判断哈希表中是否可能有该键值，若没有则直接跳过查哈希表的步骤），将匹配到的键值对合并输出（可能遇到表太大，导致哈希表无法一次性放入内存，使用外部哈希算法，首先将原数据分成多个partition写入磁盘，每次对内表和外表处于同一个id的partition进行hashjoin）
3. NestedIndexJoin：与NestedLoopJoin差不多，每次拿到坐标的一条记录时，如果Join On的条件在右表上有索引，则使用索引查出来RID，并emit新的记录
## 3.3 Aggregation Executors
1. 大致思路是以group by的键建立一张哈希表，value可能是sum、count，在遍历每一条记录时，由key匹配到哈希表的对应桶，对sum来说就在value上加值，对count来说就在value上加1
## 3.4 执行计划优化
1. Sort + Limit转化为TopN，后序遍历，判断是否有limit节点下存在sort节点，由随机选择算法实现，再对TopN个数排一次序
2. 谓词下推：先序遍历，判断是否where条件仅涉及一个表和一个常数，不断下推
3. 调整join顺序，根据左右表的大小，将较小的表作为外表

# Project 4 Concurrency Control
## 4.1 数据结构
```C++
  // 每一个资源上都会存在一个锁请求队列，表示当前这个资源上加了什么锁以及还要申请什么锁
  class LockRequestQueue {
   public:
    /** List of lock requests for the same resource (table or row) */
    std::list<std::shared_ptr<LockRequest>> request_queue_;
    /** For notifying blocked transactions on this rid */
    std::condition_variable cv_;
    /** txn_id of an upgrading transaction (if any) */
    txn_id_t upgrading_ = INVALID_TXN_ID;
    /** coordination */
    std::mutex latch_;
  };

  // 锁管理器
  class LockManager {
    /** 管理每个table对应的LockRequestQueue */
    std::unordered_map<table_oid_t, std::shared_ptr<LockRequestQueue>> table_lock_map_;

    /** 管理每个RID对应的LockRequestQueue */
    std::unordered_map<RID, std::shared_ptr<LockRequestQueue>> row_lock_map_;

    /** 死锁检测线程 */
    std::thread *cycle_detection_thread_;
    /** 事务依赖图，当事务A申请的锁被事务B占有时，会在依赖图上加一条有向边  */
    std::unordered_map<txn_id_t, std::vector<txn_id_t>> waits_for_;
    /** 维护了当前存活的事务id*/
    std::set<txn_id_t> alive_txns_;
    /** 记录事务睡在哪个LockRequestQueue的条件变量上，方便死锁检测时唤醒*/
    std::unordered_map<txn_id_t, LockRequestQueue*> txn_wait_map_;
  };
```

## 4.2 锁申请流程（表锁为例）
1. 首先进行2PL检查，如txn处于shrinking状态时不能加互斥锁，只有在READ_COMMITTED级别可以加共享锁等……
2. 获取或创建加锁资源的锁请求队列，判断请求队列中是否有该txn的加锁请求，是进入3，否则进入4
3. 判断能否进行锁升级（每个资源同时只能有一个锁升级请求），如果可以进行锁升级，将原来的锁请求取消，并在队列未获得批准的第一个锁请求前加入该锁升级请求（锁升级请求具有优先性），进入5
4. 在队列末尾加入该锁请求
5. 尝试在该线程中尽可能批准锁请求队列中的锁请求
6. 循环判断请求是否已批准，如果未批准，调用cv.wait()将自己阻塞，如果已批准则进入7
7. 查看当前txn是否因为引起了死锁而被ABORT，如果是则从请求队列中删除锁请求
8. 更改txn保存的锁状态

## 4.3 解锁流程（表锁为例）
1. 检查该事务是否有这个锁，以及这个表锁下是否有未解锁的行锁
2. 在锁请求队列中删除该锁（之前已被批准）
3. 根据隔离级别修改事务状态是否由GROWING进入SHRINGKING
4. 尽量批准锁请求队列的锁请求，并唤醒睡在该请求队列上的所有线程（可能会导致惊群效应，可以为每个请求都分配一个condition variable，纯瞎写，没有太理解什么是惊群）

## 4.4 死锁检测
基本思路：每轮死锁检测开启时，遍历所有资源的锁请求队列，建立等待图，如果发现等待图中有环，则找到环中最年轻的txn，abort该事务，对该事务进行回滚（每个事务都会有一个写记录队列，记录了执行哪些insert/delete操作，回滚时将这些操作产生的元数据进行更改）

## 4.5 Concurrency Transaction
基本思路：读时，在Init中对表加IS锁，在Next中对行加S锁，根据隔离级别选择释放锁；写时，在Init中对表加IX锁，在next中写一行数据，对该数据加X锁