from typing import Optional

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
from __static__ import cast, int32, int64, box, unbox

# Task IDs
I_IDLE = 1
I_WORK = 2
I_HANDLERA = 3
I_HANDLERB = 4
I_DEVA = 5
I_DEVB = 6

# Packet types
K_DEV = 1000
K_WORK = 1001

# Packet

BUFSIZE = 4

BUFSIZE_RANGE = range(BUFSIZE)


class Packet(object):

    def __init__(self, l: Optional["Packet"], i: int, k: int) -> None:
        self.link: Optional["Packet"] = l
        self.ident  = i
        self.kind  = k
        self.datum = 0
        self.data = [0] * BUFSIZE

    def append_to(self, lst: Optional["Packet"]) -> "Packet":
        self.link = None
        if lst is None:
            return self
        else:
            p: Optional["Packet"] = lst
            next: Optional["Packet"] = p.link
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
        self.pending: Optional[Packet] = None


class IdleTaskRec(TaskRec):

    def __init__(self) -> None:
        self.control: int32 = 1
        self.count: int32 = 10000


class HandlerTaskRec(TaskRec):

    def __init__(self) -> None:
        self.work_in: Optional[Packet] = None
        self.device_in: Optional[Packet] = None

    def workInAdd(self, p: Packet) -> Optional[Packet]:
        self.work_in = p.append_to(self.work_in)
        return self.work_in

    def deviceInAdd(self, p: Packet) -> Optional[Packet]:
        self.device_in = p.append_to(self.device_in)
        return self.device_in


class WorkerTaskRec(TaskRec):

    def __init__(self) -> None:
        self.destination = I_HANDLERA
        self.count: int32 = 0
# Task


class TaskState(object):

    def __init__(self) -> None:
        self.packet_pending: bool = True
        self.task_waiting: bool = False
        self.task_holding: bool = False

    def packetPending(self) -> "TaskState":
        self.packet_pending = True
        self.task_waiting = False
        self.task_holding = False
        return self

    def waiting(self) -> "TaskState":
        self.packet_pending = False
        self.task_waiting = True
        self.task_holding = False
        return self

    def running(self) -> "TaskState":
        self.packet_pending = False
        self.task_waiting = False
        self.task_holding = False
        return self

    def waitingWithPacket(self) -> "TaskState":
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

    def isTaskHoldingOrWaiting(self):
        return self.task_holding or (not self.packet_pending and self.task_waiting)

    def isWaitingWithPacket(self):
        return self.packet_pending and self.task_waiting and not self.task_holding


tracing = False
layout = 0


def trace(a):
    global layout
    layout -= 1
    if layout <= 0:
        print()
        layout = 50
    print(a, end='')


TASKTABSIZE = 10


class TaskWorkArea(object):

    def __init__(self) -> None:
        self.taskTab = [None] * TASKTABSIZE

        self.taskList: Optional["Task"] = None

        self.holdCount: int32 = 0
        self.qpktCount: int32 = 0


taskWorkArea: TaskWorkArea = TaskWorkArea()


class Task(TaskState):

    def __init__(self, i: int, p: int, w: Optional[Packet], initialState: TaskState, r: TaskRec) -> None:
        wa: TaskWorkArea = taskWorkArea
        self.link: Optional["Task"] = wa.taskList
        self.ident: int = i
        self.priority: int64 = unbox(p)
        self.input: Optional[Packet] = w

        self.packet_pending = initialState.isPacketPending()
        self.task_waiting = initialState.isTaskWaiting()
        self.task_holding = initialState.isTaskHolding()

        self.handle = r

        wa.taskList = self
        wa.taskTab[i] = self

    def fn(self, pkt, r) -> Optional["Task"]:
        raise NotImplementedError

    def addPacket(self, p: Packet, old: "Task") -> "Task":
        if self.input is None:
            self.input = p
            self.packet_pending = True
            if self.priority > old.priority:
                return self
        else:
            p.append_to(self.input)
        return old

    def runTask(self) -> Optional["Task"]:
        if self.isWaitingWithPacket():
            msg: Optional["Packet"] = self.input
            if msg is not None:
                self.input = msg.link
                if self.input is None:
                    self.running()
                else:
                    self.packetPending()
        else:
            msg = None

        return self.fn(msg, self.handle)

    def waitTask(self) -> "Task":
        self.task_waiting = True
        return self

    def hold(self) -> Optional["Task"]:
        taskWorkArea.holdCount += 1
        self.task_holding = True
        return self.link

    def release(self, i) -> "Task":
        t: Task = self.findtcb(i)
        t.task_holding = False
        if t.priority > self.priority:
            return t
        else:
            return self

    def qpkt(self, pkt: Packet) -> "Task":
        t: "Task" = self.findtcb(pkt.ident)
        taskWorkArea.qpktCount += 1
        pkt.link = None
        pkt.ident = self.ident
        return t.addPacket(pkt, self)

    def findtcb(self, id) -> "Task":
        t = taskWorkArea.taskTab[id]
        if t is None:
            pass
        return cast(Task, t)


# DeviceTask


class DeviceTask(Task):

    def __init__(self, i: int, p: int, w: Optional[Packet], s: TaskState, r: DeviceTaskRec) -> None:
        Task.__init__(self, i, p, w, s, r)

    def fn(self, pkt: Optional[Packet], r: DeviceTaskRec) -> Task:
        d: DeviceTaskRec = r
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

    def __init__(self, i: int, p: int, w: Packet, s: TaskState, r: HandlerTaskRec) -> None:
        Task.__init__(self, i, p, w, s, r)

    def fn(self, pkt: Optional[Packet], r: HandlerTaskRec) -> Task:
        h: HandlerTaskRec = r
        if pkt is not None:
            if pkt.kind == K_WORK:
                h.workInAdd(pkt)
            else:
                h.deviceInAdd(pkt)
        work: Optional[Packet] = h.work_in
        if work is None:
            return self.waitTask()
        count = work.datum
        if count >= BUFSIZE:
            h.work_in = work.link
            return self.qpkt(work)

        dev: Optional[Packet] = h.device_in
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

    def fn(self, pkt: Optional[Packet], r: IdleTaskRec) -> Optional[Task]:
        i: IdleTaskRec = r
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


A = ord('A')


class WorkTask(Task):

    def __init__(self, i: int, p: int, w: Packet, s: TaskState, r: WorkerTaskRec) -> None:
        Task.__init__(self, i, p, w, s, r)

    def fn(self, pkt: Optional[Packet], r: WorkerTaskRec) -> Task:
        w: WorkerTaskRec = r
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
            x: int32 = w.count + 1
            w.count = x
            if w.count > 26:
                w.count = 1
            pkt.data[i] = A + box(w.count) - 1
            i = i + 1

        return self.qpkt(pkt)


def schedule() -> None:
    t: Optional[Task] = taskWorkArea.taskList
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

            wkq: Optional[Packet] = Packet(None, 0, K_WORK)
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
                print('err')
                return False
        return True


if __name__ == "__main__":
    Richards().run(10)
