/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "common/stats/ThreadLocalStats.h"

namespace folly {
template <class T, class Tag, class AccessMode>
class ThreadLocal;
}

namespace facebook {
namespace eden {

/**
 * A tag class for using with folly::ThreadLocal when storing EdenStats.
 */
class EdenStatsTag {};
class EdenStats;

using ThreadLocalEdenStats = folly::ThreadLocal<EdenStats, EdenStatsTag, void>;

/**
 * EdenStats contains various thread-local stats structures.
 *
 * Each EdenStats object should only be used from a single thread.
 * The ThreadLocalEdenStats object should be used to maintain one EdenStats
 * object for each thread that needs to access/update the stats.
 */
class EdenStats : public facebook::stats::ThreadLocalStatsT<
                      facebook::stats::TLStatsThreadSafe> {
 public:
  using Histogram = TLHistogram;

  explicit EdenStats();

  // We track latency in units of microseconds, hence the _us suffix
  // in the histogram names below.

  Histogram lookup{createHistogram("fuse.lookup_us")};
  Histogram forget{createHistogram("fuse.forget_us")};
  Histogram getattr{createHistogram("fuse.getattr_us")};
  Histogram setattr{createHistogram("fuse.setattr_us")};
  Histogram readlink{createHistogram("fuse.readlink_us")};
  Histogram mknod{createHistogram("fuse.mknod_us")};
  Histogram mkdir{createHistogram("fuse.mkdir_us")};
  Histogram unlink{createHistogram("fuse.unlink_us")};
  Histogram rmdir{createHistogram("fuse.rmdir_us")};
  Histogram symlink{createHistogram("fuse.symlink_us")};
  Histogram rename{createHistogram("fuse.rename_us")};
  Histogram link{createHistogram("fuse.link_us")};
  Histogram open{createHistogram("fuse.open_us")};
  Histogram read{createHistogram("fuse.read_us")};
  Histogram write{createHistogram("fuse.write_us")};
  Histogram flush{createHistogram("fuse.flush_us")};
  Histogram release{createHistogram("fuse.release_us")};
  Histogram fsync{createHistogram("fuse.fsync_us")};
  Histogram opendir{createHistogram("fuse.opendir_us")};
  Histogram readdir{createHistogram("fuse.readdir_us")};
  Histogram releasedir{createHistogram("fuse.releasedir_us")};
  Histogram fsyncdir{createHistogram("fuse.fsyncdir_us")};
  Histogram statfs{createHistogram("fuse.statfs_us")};
  Histogram setxattr{createHistogram("fuse.setxattr_us")};
  Histogram getxattr{createHistogram("fuse.getxattr_us")};
  Histogram listxattr{createHistogram("fuse.listxattr_us")};
  Histogram removexattr{createHistogram("fuse.removexattr_us")};
  Histogram access{createHistogram("fuse.access_us")};
  Histogram create{createHistogram("fuse.create_us")};
  Histogram bmap{createHistogram("fuse.bmap_us")};
  Histogram ioctl{createHistogram("fuse.ioctl_us")};
  Histogram poll{createHistogram("fuse.poll_us")};
  Histogram forgetmulti{createHistogram("fuse.forgetmulti_us")};

  // Since we can potentially finish a request in a different
  // thread from the one used to initiate it, we use HistogramPtr
  // as a helper for referencing the pointer-to-member that we
  // want to update at the end of the request.
  using HistogramPtr = Histogram EdenStats::*;

  /** Record a the latency for an operation.
   * item is the pointer-to-member for one of the histograms defined
   * above.
   * elapsed is the duration of the operation, measured in microseconds.
   * now is the current steady clock value in seconds.
   * (Once we open source the common stats code we can eliminate the
   * now parameter from this method). */
  void recordLatency(
      HistogramPtr item,
      std::chrono::microseconds elapsed,
      std::chrono::seconds now);

 private:
  Histogram createHistogram(const std::string& name);
};

} // namespace eden
} // namespace facebook
