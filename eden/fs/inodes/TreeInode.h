/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once
#include <folly/Optional.h>
#include "eden/fs/inodes/InodeBase.h"
#include "eden/fs/model/Hash.h"
#include "eden/fuse/InodeNameManager.h"
#include "eden/utils/PathMap.h"

namespace facebook {
namespace eden {

class EdenMount;
class FileHandle;
class ObjectStore;
class Tree;
class Overlay;

// Represents a Tree instance in a form that FUSE can consume
class TreeInode : public InodeBase {
 public:
  /** Represents a directory entry.
   * A directory entry holds the combined Tree and Overlay data;
   * if a directory is only partially materialized the entire
   * directory contents be part of this data, but the individual
   * entries will indicate whether they have been materialized or not.
   */
  struct Entry {
    /** The complete st_mode value for this entry */
    mode_t mode;
    /** If !materialized, the blob or tree hash for this entry in
     * the local store */
    folly::Optional<Hash> hash;
    /** true if the entry has been materialized to the overlay.
     * For a directory this means that the directory exists, for
     * a file it means that the file exists */
    bool materialized{false};
  };

  /** Represents a directory in the overlay */
  struct Dir {
    /** The direct children of this directory */
    PathMap<std::unique_ptr<Entry>> entries;
    /** If the origin of this dir was a Tree, the hash of that tree */
    folly::Optional<Hash> treeHash;

    /** true if the dir has been materialized to the overlay.
     * If the contents match the original tree, this is false. */
    bool materialized{false};
  };

  /** Holds the results of a create operation.
   *
   * It is important that the file handle creation respect O_EXCL if
   * it set in the flags parameter to TreeInode::create.
   */
  struct CreateResult {
    /// file attributes and cache ttls.
    fusell::Dispatcher::Attr attr;
    /// The newly created inode instance.
    InodePtr inode;
    /// The newly opened file handle.
    std::shared_ptr<FileHandle> file;
    /// The newly created node record from the name manager.
    std::shared_ptr<fusell::InodeNameManager::Node> node;

    explicit CreateResult(const fusell::MountPoint* mount) : attr(mount) {}
  };

  TreeInode(
      EdenMount* mount,
      fuse_ino_t ino,
      TreeInodePtr parent,
      PathComponentPiece name,
      Entry* entry,
      std::unique_ptr<Tree>&& tree);

  /// Construct an inode that only has backing in the Overlay area
  TreeInode(
      EdenMount* mount,
      fuse_ino_t ino,
      TreeInodePtr parent,
      PathComponentPiece name,
      Entry* entry,
      Dir&& dir);

  /// Constructors for the root TreeInode
  TreeInode(EdenMount* mount, std::unique_ptr<Tree>&& tree);
  TreeInode(EdenMount* mount, Dir&& tree);

  ~TreeInode();

  folly::Future<fusell::Dispatcher::Attr> getattr() override;
  fusell::Dispatcher::Attr getAttrLocked(const Dir* contents);

  /** Implements the InodeBase method used by the Dispatcher
   * to create the Inode instance for a given name */
  folly::Future<InodePtr> getChildByName(PathComponentPiece namepiece);

  folly::Future<std::shared_ptr<fusell::DirHandle>> opendir(
      const struct fuse_file_info& fi);
  folly::Future<folly::Unit> rename(
      PathComponentPiece name,
      TreeInodePtr newParent,
      PathComponentPiece newName);

  bool canForget() override;

  void renameHelper(
      Dir* sourceContents,
      RelativePathPiece sourceName,
      Dir* destContents,
      RelativePathPiece destName);

  fuse_ino_t getParent() const;
  fuse_ino_t getInode() const;

  const folly::Synchronized<Dir>& getContents() const {
    return contents_;
  }
  folly::Synchronized<Dir>& getContents() {
    return contents_;
  }

  /**
   * Get the EdenMount that this TreeInode belongs to.
   *
   * The EdenMount is guaranteed to remain valid for at least the lifetime of
   * the TreeInode object.
   */
  EdenMount* getMount() const;

  /**
   * Get the ObjectStore for this mount point.
   *
   * The ObjectStore is guaranteed to remain valid for at least the lifetime of
   * the TreeInode object.  (The ObjectStore is owned by the EdenMount.)
   */
  ObjectStore* getStore() const;

  const std::shared_ptr<Overlay>& getOverlay() const;
  folly::Future<CreateResult>
  create(PathComponentPiece name, mode_t mode, int flags);
  folly::Future<fuse_entry_param> symlink(
      PathComponentPiece name,
      folly::StringPiece contents);

  folly::Future<fuse_entry_param> mkdir(PathComponentPiece name, mode_t mode);
  folly::Future<folly::Unit> unlink(PathComponentPiece name);
  folly::Future<folly::Unit> rmdir(PathComponentPiece name);

  /** Called in a thrift context to switch the active snapshot.
   * Since this is called in a thrift context, RequestData::get() won't
   * return the usual results and the appropriate information must
   * be passed down from the thrift server itself.
   */
  void performCheckout(const Hash& hash);

  /** Ensure that the overlay is tracking metadata for this inode
   * This is required whenever we are about to make a structural change
   * in the tree; renames, creation, deletion. */
  void materializeDirAndParents();

  fusell::InodeNameManager* getNameMgr() const;

  /** Horribly named function that resolves the existing inode for a name,
   * falling back to creating and populating it, while we hold a lock
   * on the Dir object.  This is needed because the equivalent lookupInodeBase
   * functionality in the dispatcher will call in to getChildByName and
   * attempt to acquire the lock */
  InodePtr lookupChildByNameLocked(
      const Dir* contents,
      PathComponentPiece name);

 private:
  /** Translates a Tree object from our store into a Dir object
   * used to track the directory in the inode */
  static Dir buildDirFromTree(const Tree* tree);

  TreeInodePtr inodePtrFromThis() {
    return std::static_pointer_cast<TreeInode>(shared_from_this());
  }

  /** Helper used to implement getChildByName and lookupChildByNameLocked */
  InodePtr getChildByNameLocked(const Dir* contents, PathComponentPiece name);

  // The EdenMount object that this inode belongs to.
  // We store this as a raw pointer since the TreeInode is part of the mount
  // point.  The EdenMount should always exist longer than any inodes it
  // contains.  (Storing a shared_ptr to the EdenMount would introduce circular
  // references which are undesirable.)
  EdenMount* const mount_{nullptr};

  folly::Synchronized<Dir> contents_;
  /** Can be nullptr for the root inode only, otherwise will be non-null */
  TreeInode::Entry* entry_{nullptr};

  // TODO: replace uses of parent_ with InodeBase::location_
  // As far as I can tell parent_ is not correctly updated when this inode is
  // renamed.
  fuse_ino_t parent_;
};
}
}
