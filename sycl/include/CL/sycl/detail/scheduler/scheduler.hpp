//==-------------- scheduler.hpp - SYCL standard header file ---------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#pragma once

#include <CL/sycl/detail/cg.hpp>
#include <CL/sycl/detail/scheduler/commands.hpp>
#include <CL/sycl/detail/sycl_mem_obj_i.hpp>

#include <memory>
#include <mutex>
#include <set>
#include <vector>

namespace cl {
namespace sycl {
namespace detail {

class queue_impl;
class event_impl;
class context_impl;

using QueueImplPtr = std::shared_ptr<detail::queue_impl>;
using EventImplPtr = std::shared_ptr<detail::event_impl>;
using ContextImplPtr = std::shared_ptr<detail::context_impl>;

// The MemObjRecord is created for each memory object used in command
// groups. There should be only one MemObjRecord for SYCL memory object.
struct MemObjRecord {
  // Contains all allocation commands for the memory object.
  std::vector<AllocaCommandBase *> MAllocaCommands;

  // Contains latest read only commands working with memory object.
  std::vector<Command *> MReadLeafs;

  // Contains latest write commands working with memory object.
  std::vector<Command *> MWriteLeafs;

  // The context which has the latest state of the memory object.
  ContextImplPtr MCurContext;

  // The flag indicates that the content of the memory object was/will be
  // modified. Used while deciding if copy back needed.
  bool MMemModified;
};

class Scheduler {
public:
  // Registers command group, adds it to the dependency graph and returns an
  // event object that can be used for waiting later. It's called by SYCL's
  // queue.submit.
  EventImplPtr addCG(std::unique_ptr<detail::CG> CommandGroup,
                     QueueImplPtr Queue);

  EventImplPtr addCopyBack(Requirement *Req);

  // Blocking call that waits for the event passed. For the eager execution mode
  // this method invokes corresponding function of device API. In the lazy
  // execution mode the method may enqueue the command associated with the event
  // passed and its dependency before calling device API.
  void waitForEvent(EventImplPtr Event);

  // Removes buffer pointed by MemObj from the graph: ensures all commands
  // accessing the memory objects are executed and triggers deallocation of all
  // memory assigned to the memory object. It's called from the sycl::buffer and
  // sycl::image destructors.
  void removeMemoryObject(detail::SYCLMemObjI *MemObj);

  EventImplPtr addHostAccessor(Requirement *Req);

  // Returns an instance of the scheduler object.
  static Scheduler &getInstance();

  // Returns list of "immediate" dependencies for the Event given.
  std::vector<EventImplPtr> getWaitList(EventImplPtr Event);

  QueueImplPtr getDefaultHostQueue() { return DefaultHostQueue; }

protected:
  Scheduler();
  static Scheduler instance;

  // The graph builder provides interfaces that can change already existing
  // graph (e.g. add/remove edges/nodes).
  class GraphBuilder {
  public:
    GraphBuilder();

    // Registers command group, adds it to the dependency graph and returns an
    // command that represents command group execution. It's called by SYCL's
    // queue::submit.
    Command *addCG(std::unique_ptr<detail::CG> CommandGroup,
                   QueueImplPtr Queue);

    Command *addCGUpdateHost(std::unique_ptr<detail::CG> CommandGroup,
                             QueueImplPtr HostQueue);

    Command *addCopyBack(Requirement *Req);
    Command *addHostAccessor(Requirement *Req, EventImplPtr &RetEvent);

    // [Provisional] Optimizes the whole graph.
    void optimize();

    // [Provisional] Optimizes subgraph that consists of command associated with
    // Event passed and its dependencies.
    void optimize(EventImplPtr Event);

    // Removes unneeded commands from the graph.
    void cleanupCommands(bool CleanupReleaseCommands = false);

    // Reschedules command passed using Queue provided. this can lead to
    // rescheduling of all dependent commands. This can be used when user
    // provides "secondary" queue to submit method which may be used when
    // command fails to enqueue/execute in primary queue.
    void rescheduleCommand(Command *Cmd, QueueImplPtr Queue);

    MemObjRecord *getMemObjRecord(SYCLMemObjI *MemObject);
    // Returns pointer to MemObjRecord for pointer to memory object.
    // Return nullptr if there the record is not found.
    MemObjRecord *getOrInsertMemObjRecord(const QueueImplPtr &Queue,
                                          Requirement *Req);

    // Removes commands that use given MemObjRecord from the graph.
    void cleanupCommandsForRecord(MemObjRecord *Record);

    // Removes MemObjRecord for memory object passed.
    void removeRecordForMemObj(SYCLMemObjI *MemObject);

    // Add new command to leafs if needed.
    void AddNodeToLeafs(MemObjRecord *Record, Command *Cmd,
                        access::mode AccessMode);

    // Removes commands from leafs.
    void UpdateLeafs(const std::set<Command *> &Cmds, MemObjRecord *Record,
                     access::mode AccessMode);

    std::vector<SYCLMemObjI *> MMemObjs;

  private:
    // The method inserts memory copy operation from the context where the
    // memory current lives to the context bound to Queue.
    MemCpyCommand *insertMemCpyCmd(MemObjRecord *Record, Requirement *Req,
                                   const QueueImplPtr &Queue,
                                   bool UseExclusiveQueue = false);

    UpdateHostRequirementCommand *
    insertUpdateHostReqCmd(MemObjRecord *Record, Requirement *Req,
                           const QueueImplPtr &Queue);

    std::set<Command *> findDepsForReq(MemObjRecord *Record, Requirement *Req,
                                       QueueImplPtr Context);

    // Searches for suitable alloca in memory record.
    AllocaCommandBase *findAllocaForReq(MemObjRecord *Record, Requirement *Req,
                                        const ContextImplPtr &Context);
    // Searches for suitable alloca in memory record.
    // If none found, creates new one.
    AllocaCommandBase *getOrCreateAllocaForReq(MemObjRecord *Record,
                                               Requirement *Req,
                                               QueueImplPtr Queue);

    void markModifiedIfWrite(MemObjRecord *Record,
                             Requirement *Req);

    // Print contents of graph to text file in DOT format
    void printGraphAsDot(const char *ModeName);
    enum PrintOptions {
      BeforeAddCG = 0,
      AfterAddCG,
      BeforeAddCopyBack,
      AfterAddCopyBack,
      BeforeAddHostAcc,
      AfterAddHostAcc,
      Size
    };
    std::array<bool, PrintOptions::Size> MPrintOptionsArray;
  };

  // The class that provides interfaces for enqueueing command and its
  // dependencies to the underlying runtime. Methods of this class must not
  // modify the graph.
  class GraphProcessor {
  public:
    // Returns a list of events that represent immediate dependencies of the
    // command associated with Event passed.
    static std::vector<EventImplPtr> getWaitList(EventImplPtr Event);

    // Wait for the command, associated with Event passed, is completed.
    static void waitForEvent(EventImplPtr Event);

    // Enqueue the command passed and all it's dependencies to the underlying
    // device. Returns true is the command is successfully enqueued. Sets
    // EnqueueResult to the specific status otherwise.
    static bool enqueueCommand(Command *Cmd, EnqueueResultT &EnqueueResult,
                               BlockingT Blocking = NON_BLOCKING);
  };

  void waitForRecordToFinish(MemObjRecord *Record);

  GraphBuilder MGraphBuilder;
  // Use read-write mutex in future.
  std::mutex MGraphLock;

  QueueImplPtr DefaultHostQueue;
};

} // namespace detail
} // namespace sycl
} // namespace cl
