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

#include <folly/File.h>
#include <folly/Range.h>
#include <gtest/gtest_prod.h>
#include <array>
#include <condition_variable>
#include <optional>
#include "eden/fs/fuse/InodeNumber.h"
#include "eden/fs/inodes/overlay/gen-cpp2/overlay_types.h"
#include "eden/fs/utils/DirType.h"
#include "eden/fs/utils/PathFuncs.h"

namespace facebook {
namespace eden {

namespace overlay {
class OverlayDir;
}
class InodePath;

/**
 * FsOverlay provides interfaces to manipulate the overlay. It stores the
 * overlay's file system attributes and is responsible for obtaining and
 * releasing its locks ("initOverlay" and "close" respectively).
 */
class FsOverlay {
 public:
  explicit FsOverlay(AbsolutePathPiece localDir) : localDir_{localDir} {}
  /**
   * Initialize the overlay, acquire the "info" file lock and load the
   * nextInodeNumber. The "close" method should be used to release these
   * resources and persist the nextInodeNumber.
   *
   * Returns the next inode number to start at when allocating new inodes.
   * If the overlay was not shutdown cleanly by the previous user then
   * std::nullopt is returned.  In this case, the caller should re-scan
   * the overlay to check for issues and compute the next inode number.
   */
  std::optional<InodeNumber> initOverlay(bool createIfNonExisting);
  /**
   *  Gracefully, shutdown the overlay, persisting the overlay's
   * nextInodeNumber.
   */
  void close(std::optional<InodeNumber> nextInodeNumber);
  /**
   * Was FsOverlay initialized - i.e., is cleanup (close) necessary.
   */
  bool initialized() const {
    return bool(infoFile_);
  }

  AbsolutePath getLocalDir() const {
    return localDir_;
  }

  /**
   *  Get the name of the subdirectory to use for the overlay data for the
   *  specified inode number.
   *
   *  We shard the inode files across the 256 subdirectories using the least
   *  significant byte.  Inode numbers are anllocated in monotonically
   * increasing order, so this helps spread them out across the subdirectories.
   */
  static void formatSubdirPath(
      folly::MutableStringPiece subdirPath,
      InodeNumber inodeNum);

  /**
   * Unconditionally create the "tmp" directory in the overlay directory.
   * It is used to support migration from an older Overlay format.
   */
  void ensureTmpDirectoryIsCreated();

  void initNewOverlay();

  void saveOverlayDir(InodeNumber inodeNumber, const overlay::OverlayDir& odir);

  std::optional<overlay::OverlayDir> loadOverlayDir(InodeNumber inodeNumber);

  void saveNextInodeNumber(InodeNumber nextInodeNumber);

  void writeNextInodeNumber(InodeNumber nextInodeNumber);

  /**
   * Return the next inode number from the kNextInodeNumberFile.  If the file
   * exists and contains a valid InodeNumber, that value is returned. If the
   * file does not exist, the optional will not have a value. If the file cannot
   * be opened or does not contain a valid InodeNumber, a SystemError is thrown.
   */
  std::optional<InodeNumber> tryLoadNextInodeNumber();
  /**
   * Scan the Inode files to find the maximumInodeNumber. Return the
   * maximumInodeNumber + 1.  The minimum value that can be returned (if no
   * files exist) would be kRootNodeId+1.
   */
  InodeNumber scanForNextInodeNumber();

  /**
   * Validate an existing overlay's info file exists, is valid and contains the
   * correct version.
   */
  void readExistingOverlay(int infoFD);

  /**
   * Helper function that creates an overlay file for a new FileInode.
   */
  folly::File createOverlayFile(
      InodeNumber inodeNumber,
      folly::ByteRange contents);

  /**
   * Helper function to write an overlay file for a FileInode with existing
   * contents.
   */
  folly::File createOverlayFile(
      InodeNumber inodeNumber,
      const folly::IOBuf& contents);

  /**
   * Remove the overlay file associated with the passed InodeNumber.
   */
  void removeOverlayFile(InodeNumber inodeNumber);

  /**
   * Validates an entry's header.
   */
  static void validateHeader(
      InodeNumber inodeNumber,
      folly::StringPiece contents,
      folly::StringPiece headerId);

  /**
   * Helper function that opens an existing overlay file,
   * checks if the file has valid header, and returns the file.
   */
  folly::File openFile(InodeNumber inodeNumber, folly::StringPiece headerId);

  /**
   * Open an existing overlay file without verifying the header.
   */
  folly::File openFileNoVerify(InodeNumber inodeNumber);

  bool hasOverlayData(InodeNumber inodeNumber);

  static constexpr folly::StringPiece kMetadataFile{"metadata.table"};

  /**
   * Constants for an header in overlay file.
   */
  static constexpr folly::StringPiece kHeaderIdentifierDir{"OVDR"};
  static constexpr folly::StringPiece kHeaderIdentifierFile{"OVFL"};
  static constexpr uint32_t kHeaderVersion = 1;
  static constexpr size_t kHeaderLength = 64;

  /**
   * The number of digits required for a decimal representation of an
   * inode number.
   */
  static constexpr size_t kMaxDecimalInodeNumberLength = 20;

 private:
  FRIEND_TEST(OverlayTest, getFilePath);
  friend class RawOverlayTest;

  /**
   * Creates header for the files stored in Overlay
   */
  static std::array<uint8_t, kHeaderLength> createHeader(
      folly::StringPiece identifier,
      uint32_t version);

  /**
   * Get the path to the file for the given inode, relative to localDir.
   *
   * Returns a null-terminated InodePath value.
   */
  static InodePath getFilePath(InodeNumber inodeNumber);

  std::optional<overlay::OverlayDir> deserializeOverlayDir(
      InodeNumber inodeNumber);

  folly::File
  createOverlayFileImpl(InodeNumber inodeNumber, iovec* iov, size_t iovCount);

 private:
  /** Path to ".eden/CLIENT/local" */
  const AbsolutePath localDir_;

  /**
   * An open file descriptor to the overlay info file.
   *
   * This is primarily used to hold a lock on the overlay for as long as we
   * are using it.  We want to ensure that only one eden process accesses the
   * Overlay directory at a time.
   */
  folly::File infoFile_;

  /**
   * An open file to the overlay directory.
   *
   * We maintain this so we can use openat(), unlinkat(), etc.
   */
  folly::File dirFile_;
};

class InodePath {
 public:
  explicit InodePath() noexcept;

  /**
   * The maximum path length for the path to a file inside the overlay
   * directory.
   *
   * This is 2 bytes for the initial subdirectory name, 1 byte for the '/',
   * 20 bytes for the inode number, and 1 byte for a null terminator.
   */
  static constexpr size_t kMaxPathLength =
      2 + 1 + FsOverlay::kMaxDecimalInodeNumberLength + 1;

  const char* c_str() const noexcept;
  /* implicit */ operator RelativePathPiece() const noexcept;

  std::array<char, kMaxPathLength>& rawData() noexcept;

 private:
  std::array<char, kMaxPathLength> path_;
};

} // namespace eden
} // namespace facebook
