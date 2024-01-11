# 聚合函数
- 在进行哈希聚合(DISTINCT/GROUP BY)时，为什么要先将整个数据集通过外部hash分成多个partition，再对每个partition使用hash进行局部聚合计算，最后再合并聚合结果？为什么不能直接对所有数据进行hash聚合计算？
> 如果聚合函数中涉及GROUP BY，则对数据集首先进行分组是天经地义的
> 如果不涉及GROUP BY，直接计算整个数据集的聚合结果，只能使用一个线程，分成多个partition分别计算聚合结果，可以使用多个线程并行计算来加速，类似于MapReduce
> 并且结果集的hashtable可能会很大，无法完整放到内存中，这时需要将数据集分为多个partition，以减小每个partition的结果集大小