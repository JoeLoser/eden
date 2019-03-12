/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "eden/fs/inodes/Differ.h"
#include <folly/Synchronized.h>
#include <folly/Utility.h>
#include <folly/futures/Future.h>
#include <folly/logging/xlog.h>
#include "eden/fs/inodes/EdenMount.h"
#include "eden/fs/inodes/InodeDiffCallback.h"
#include "eden/fs/model/Tree.h"
#include "eden/fs/model/TreeEntry.h"
#include "eden/fs/store/ObjectStore.h"
#include "eden/fs/utils/PathFuncs.h"

namespace facebook {
namespace eden {
namespace {
class ThriftStatusCallback : public InodeDiffCallback {
 public:
  void ignoredFile(RelativePathPiece path) override {
    data_.wlock()->emplace(path.stringPiece().str(), ScmFileStatus::IGNORED);
  }

  void untrackedFile(RelativePathPiece path) override {
    data_.wlock()->emplace(path.stringPiece().str(), ScmFileStatus::ADDED);
  }

  void removedFile(
      RelativePathPiece path,
      const TreeEntry& /* sourceControlEntry */) override {
    data_.wlock()->emplace(path.stringPiece().str(), ScmFileStatus::REMOVED);
  }

  void modifiedFile(
      RelativePathPiece path,
      const TreeEntry& /* sourceControlEntry */) override {
    data_.wlock()->emplace(path.stringPiece().str(), ScmFileStatus::MODIFIED);
  }

  void diffError(RelativePathPiece path, const folly::exception_wrapper& ew)
      override {
    // TODO: It would be nice to have a mechanism to return error info as part
    // of the thrift result.
    XLOG(WARNING) << "error computing status data for " << path << ": "
                  << folly::exceptionStr(ew);
  }

  /**
   * Extract the ScmStatus object from this callback.
   *
   * This method should be called no more than once, as this destructively
   * moves the results out of the callback.  It should only be invoked after
   * the diff operation has completed.
   */
  ScmStatus extractStatus() {
    ScmStatus status;

    {
      auto data = data_.wlock();
      status.entries.swap(*data);
    }

    return status;
  }

 private:
  folly::Synchronized<std::map<std::string, ScmFileStatus>> data_;
};
} // unnamed namespace

char scmStatusCodeChar(ScmFileStatus code) {
  switch (code) {
    case ScmFileStatus::ADDED:
      return 'A';
    case ScmFileStatus::MODIFIED:
      return 'M';
    case ScmFileStatus::REMOVED:
      return 'R';
    case ScmFileStatus::IGNORED:
      return 'I';
  }
  throw std::runtime_error(folly::to<std::string>(
      "Unrecognized ScmFileStatus: ",
      folly::to_underlying_type(code)));
}

std::ostream& operator<<(std::ostream& os, const ScmStatus& status) {
  os << "{";
  for (const auto& pair : status.get_entries()) {
    os << scmStatusCodeChar(pair.second) << " " << pair.first << "; ";
  }
  os << "}";
  return os;
}

folly::Future<std::unique_ptr<ScmStatus>>
diffMountForStatus(const EdenMount& mount, Hash commitHash, bool listIgnored) {
  auto callback = std::make_unique<ThriftStatusCallback>();
  auto callbackPtr = callback.get();
  return mount.diff(callbackPtr, commitHash, listIgnored)
      .thenValue([callback = std::move(callback)](auto&&) {
        return std::make_unique<ScmStatus>(callback->extractStatus());
      });
}

} // namespace eden
} // namespace facebook
