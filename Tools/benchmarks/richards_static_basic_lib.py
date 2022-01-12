# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
"""
based on a Java version:
 Based on original version written in BCPL by Dr Martin Richards
 in 1981 at Cambridge University Computer Laboratory, England
 and a C++ version derived from a Smalltalk version written by
 L Peter Deutsch.
 Java version:  Copyright (C) 1995 Sun Microsystems, Inc.
 Translation from C++, Mario Wolczko
 Outer loop added by Alex Jacoby
"""
from __future__ import annotations
import __static__
from __static__ import cast
import sys
from typing import Final, List

# Task IDs
I_IDLE: Final[int] = 1
I_WORK: Final[int] = 2
I_HANDLERA: Final[int] = 3
I_HANDLERB: Final[int] = 4
I_DEVA: Final[int] = 5
I_DEVB: Final[int] = 6

# Packet types
K_DEV: Final[int] = 1000
K_WORK: Final[int] = 1001

# Packet

BUFSIZE: Final[int] = 4

BUFSIZE_RANGE: List[int] = list(range(BUFSIZE))


class Packet(object):

    def __init__(self, l: Packet | None, i: int, k: int) -> None:
        self.link: Packet | None = l
        self.ident: int = i
        self.kind: int = k
        self.datum: int = 0
        self.data: List[int] = [0] * BUFSIZE

    def append_to(self, lst: Packet | None) -> Packet:
        self.link = None
        if lst is None:
            return self
        else:
            p = lst
            next = p.link
            while next is not None:
                p = next
                next = p.link
            p.link = self
            return lst

# Task Records


class TaskRec(object):
    pass


class DeviceTaskRec(TaskRec):

    def __init__(self) -> None:
        self.pending: Packet | None = None


class IdleTaskRec(TaskRec):

    def __init__(self) -> None:
        self.control: int = 1
        self.count: int = 10000


class HandlerTaskRec(TaskRec):

    def __init__(self) -> None:
        self.work_in: Packet | None = None
        self.device_in: Packet | None = None

    def workInAdd(self, p: Packet) -> Packet | None:
        self.work_in = p.append_to(self.work_in)
        return self.work_in

    def deviceInAdd(self, p: Packet) -> Packet | None:
        self.device_in = p.append_to(self.device_in)
        return self.device_in


class WorkerTaskRec(TaskRec):

    def __init__(self) -> None:
        self.destination: int = I_HANDLERA
        self.count: int = 0
# Task


class TaskState(object):

    def __init__(self) -> None:
        self.packet_pending: bool = True
        self.task_waiting: bool = False
        self.task_holding: bool = False

    def packetPending(self) -> TaskState:
        self.packet_pending = True
        self.task_waiting = False
        self.task_holding = False
        return self

    def waiting(self) -> TaskState:
        self.packet_pending = False
        self.task_waiting = True
        self.task_holding = False
        return self

    def running(self) -> TaskState:
        self.packet_pending = False
        self.task_waiting = False
        self.task_holding = False
        return self

    def waitingWithPacket(self) -> TaskState:
        self.packet_pending = True
        self.task_waiting = True
        self.task_holding = False
        return self

    def isPacketPending(self) -> bool:
        return self.packet_pending

    def isTaskWaiting(self) -> bool:
        return self.task_waiting

    def isTaskHolding(self) -> bool:
        return self.task_holding

    def isTaskHoldingOrWaiting(self) -> bool:
        return self.task_holding or (not self.packet_pending and self.task_waiting)

    def isWaitingWithPacket(self) -> bool:
        return self.packet_pending and self.task_waiting and not self.task_holding


tracing: bool = False
layout: int = 0


def trace(a):
    global layout
    layout -= 1
    if layout <= 0:
        print()
        layout = 50
    print(a, end='')


TASKTABSIZE: Final[int] = 10


class TaskWorkArea(object):

    def __init__(self) -> None:
        self.taskTab: List[Task | None] = [None] * TASKTABSIZE

        self.taskList: Task | None = None

        self.holdCount: int = 0
        self.qpktCount: int = 0


taskWorkArea: Final[TaskWorkArea] = TaskWorkArea()


class Task(TaskState):

    def __init__(self, i: int, p: int, w: Packet | None, initialState: TaskState, r: TaskRec) -> None:
        self.link: Task | None = taskWorkArea.taskList
        self.ident: int = i
        self.priority: int = p
        self.input: Packet | None = w

        self.packet_pending: bool = initialState.isPacketPending()
        self.task_waiting: bool = initialState.isTaskWaiting()
        self.task_holding: bool = initialState.isTaskHolding()

        self.handle: TaskRec = r

        taskWorkArea.taskList = self
        taskWorkArea.taskTab[i] = self

    def fn(self, pkt: Packet | None, r: TaskRec) -> Task | None:
        raise NotImplementedError

    def addPacket(self, p: Packet, old: Task) -> Task:
        if self.input is None:
            self.input = p
            self.packet_pending = True
            if self.priority > old.priority:
                return self
        else:
            p.append_to(self.input)
        return old

    def runTask(self) -> Task | None:
        if self.isWaitingWithPacket():
            msg = self.input
            assert msg is not None
            self.input = msg.link
            if self.input is None:
                self.running()
            else:
                self.packetPending()
        else:
            msg = None

        return self.fn(msg, self.handle)

    def waitTask(self) -> Task:
        self.task_waiting = True
        return self

    def hold(self) -> Task | None:
        taskWorkArea.holdCount += 1
        self.task_holding = True
        return self.link

    def release(self, i: int) -> Task:
        t = self.findtcb(i)
        t.task_holding = False
        if t.priority > self.priority:
            return t
        else:
            return self

    def qpkt(self, pkt: Packet) -> Task:
        t = self.findtcb(pkt.ident)
        taskWorkArea.qpktCount += 1
        pkt.link = None
        pkt.ident = self.ident
        return t.addPacket(pkt, self)

    def findtcb(self, id: int) -> Task:
        t = taskWorkArea.taskTab[id]
        assert t is not None
        return t


# DeviceTask


class DeviceTask(Task):

    def __init__(self, i: int, p: int, w: Packet | None, s: TaskState, r: DeviceTaskRec) -> None:
        Task.__init__(self, i, p, w, s, r)

    def fn(self, pkt: Packet | None, r: TaskRec) -> Task:
        d: DeviceTaskRec = cast(DeviceTaskRec, r)
        if pkt is None:
            pkt = d.pending
            if pkt is None:
                return self.waitTask()
            else:
                d.pending = None
                return self.qpkt(pkt)
        else:
            d.pending = pkt
            if tracing:
                trace(pkt.datum)
            return cast(Task, self.hold())


class HandlerTask(Task):

    def __init__(self, i: int, p: int, w: Packet | None, s: TaskState, r: HandlerTaskRec) -> None:
        Task.__init__(self, i, p, w, s, r)

    def fn(self, pkt: Packet | None, r: TaskRec) -> Task:
        h: HandlerTaskRec = cast(HandlerTaskRec, r)
        if pkt is not None:
            if pkt.kind == K_WORK:
                h.workInAdd(pkt)
            else:
                h.deviceInAdd(pkt)
        work = h.work_in
        if work is None:
            return self.waitTask()
        count = work.datum
        if count >= BUFSIZE:
            h.work_in = work.link
            return self.qpkt(work)

        dev = h.device_in
        if dev is None:
            return self.waitTask()

        h.device_in = dev.link
        dev.datum = work.data[count]
        work.datum = count + 1
        return self.qpkt(dev)

# IdleTask


class IdleTask(Task):

    def __init__(self, i: int, p: int, w: int, s: TaskState, r: IdleTaskRec) -> None:
        Task.__init__(self, i, 0, None, s, r)

    def fn(self, pkt: Packet | None, r: TaskRec) -> Task | None:
        i: IdleTaskRec = cast(IdleTaskRec, r)
        i.count -= 1
        if i.count == 0:
            return self.hold()
        elif i.control & 1 == 0:
            i.control //= 2
            return self.release(I_DEVA)
        else:
            i.control = i.control // 2 ^ 0xd008
            return self.release(I_DEVB)


# WorkTask


A: Final[int] = 65 # ord('A')


class WorkTask(Task):

    def __init__(self, i: int, p: int, w: Packet | None, s: TaskState, r: WorkerTaskRec) -> None:
        Task.__init__(self, i, p, w, s, r)

    def fn(self, pkt: Packet | None, r: TaskRec) -> Task:
        w: WorkerTaskRec = cast(WorkerTaskRec, r)
        if pkt is None:
            return self.waitTask()

        if w.destination == I_HANDLERA:
            dest = I_HANDLERB
        else:
            dest = I_HANDLERA

        w.destination = dest
        pkt.ident = dest
        pkt.datum = 0

        i = 0
        while i < BUFSIZE:
            x = w.count + 1
            w.count = x
            if w.count > 26:
                w.count = 1
            pkt.data[i] = A + w.count - 1
            i = i + 1

        return self.qpkt(pkt)


def schedule() -> None:
    t: Task | None = taskWorkArea.taskList
    while t is not None:
        if tracing:
            print("tcb =", t.ident)

        if t.isTaskHoldingOrWaiting():
            t = t.link
        else:
            if tracing:
                trace(chr(ord("0") + t.ident))
            t = t.runTask()


class Richards(object):

    def run(self, iterations: int) -> bool:
        for i in range(iterations):
            taskWorkArea.holdCount = 0
            taskWorkArea.qpktCount = 0

            IdleTask(I_IDLE, 1, 10000, TaskState().running(), IdleTaskRec())

            wkq = Packet(None, 0, K_WORK)
            wkq = Packet(wkq, 0, K_WORK)
            WorkTask(I_WORK, 1000, wkq, TaskState(
            ).waitingWithPacket(), WorkerTaskRec())

            wkq = Packet(None, I_DEVA, K_DEV)
            wkq = Packet(wkq, I_DEVA, K_DEV)
            wkq = Packet(wkq, I_DEVA, K_DEV)
            HandlerTask(I_HANDLERA, 2000, wkq, TaskState(
            ).waitingWithPacket(), HandlerTaskRec())

            wkq = Packet(None, I_DEVB, K_DEV)
            wkq = Packet(wkq, I_DEVB, K_DEV)
            wkq = Packet(wkq, I_DEVB, K_DEV)
            HandlerTask(I_HANDLERB, 3000, wkq, TaskState(
            ).waitingWithPacket(), HandlerTaskRec())

            wkq = None
            DeviceTask(I_DEVA, 4000, wkq,
                       TaskState().waiting(), DeviceTaskRec())
            DeviceTask(I_DEVB, 5000, wkq,
                       TaskState().waiting(), DeviceTaskRec())

            schedule()

            if taskWorkArea.holdCount == 9297 and taskWorkArea.qpktCount == 23246:
                pass
            else:
                return False
        return True


if __name__ == "__main__":
    num_iterations = 1
    if len(sys.argv) > 1:
        num_iterations = int(sys.argv[1])
    Richards().run(num_iterations)
