# Object

DAOS object stores user's data, it is identified by object ID which is unique
within the DAOS conatiner it belongs to. Objects can be distributed across any
target of the pool for both performance and resilience.
DAOS object in DAOS storage model is shown in the diagram -
<b>object in storage model</b>
![../../doc/graph/Fig_002.png]
The object module implements the object I/O stack.

## KV sotre, dkey and akey

Each DAOS object is a Key-Value store with locality feature. The key is split
into a <b>dkey</b> (distribution key) and an <b>akey</b> (attribute key). All
entries with the same dkey are guaranteed to be collocated on the same targets.
Enumeration of the akeys of a dkey is provided.

The value can be either atomic <b>single value</b> (i.e. value replaced on
update) or a <b>byte array</b> (i.e. arbitrary extent fetch/update). 

## Object Schema and object class

To avoid scaling problems and overhead common to traditional storage stack,
DAOS objects are intentionally very simple. No default object metadata beyond
the type and schema (object class ownership) are provided. This means that the
system does not maintain time, size, owner, permissions and opener tracking
attributes. All object operations are idempotent and can be processed multiple
times with the same results. This guarantees that any operations can be repeated
until successful or abandoned, without having to handle execute-once semantic
and reply reconstruction.

The DAOS <b>object schema</b> describes the definitions for object types, data
protection methods, and data distribution strategies. An <b>object class</b> has
a unique class ID, which is a 16-bit value, and can represent a category of
objects that use the same schema and schema attributes. DAOS provides some
pre-defined object class for the most common use (see daos_obj_classes). In
addition user can register customized object class by daos_obj_register_class()
(now it is not implemented yet). A successfully registered object class is
stored as container metadata; it is valid in the lifetime of the container.

The object class ID is embedded in object ID. By daos_obj_generate_id() user can
generate an object ID for the specific object class ID. DAOS uses this class ID
to find the corresponding object class, and then distribute and protect object
data based on algorithm descriptions of this class.

## Data protection method

Two types data protection methods supported by DAOS - replication and erasure
coding.

### Replication

Replication ensures high availability of object data because objects are
accessible while any replica exists. Replication can also increase read
bandwidth by allowing concurrent reads from different replicas.

#### Client replication

Client replication is the mode that it is synchronous and fully in the client
stack, to provide high concurrency and low latency I/O for the upper layer.
- I/O requests against replicas are directly issued via DAOS client; there is
  no sequential guarantee on writes in the same epoch, and concurrent writes for
  a same object can arrive at different replicas in an arbitrary order.
- Because there is no communication between servers in this way, there is no
  consistent guarantee if there are overlapped writes or KV updates in the same
  epoch. The DAOS server should detect overlapped updates in the same epoch, and
  return errors or warnings for the updates to the client. The only exception is
  multiple updates to the same extent or KV having the exactly same data. In
  this case, it is allowed because these updates could potentially be the
  resending requests.

Furthermore, when failure occurs, the client replication protocol still needs
some extra mechanism to enforce consistency between replicas:
- If the DAOS client can capture the failure, e.g. a target failed during I/O,
  because the DAOS client can complete this I/O by switching to another target
  and resubmitting request. At the meanwhile, the DAOS servers can rebuild the
  missing replica in the background. Therefore, DAOS can still guarantee data
  consistency between replicas. This process is transparent to DAOS user.
- If DAOS cannot capture the failure, for example, the DAOS client itself
  crashed before successfully updating all replicas so some replicas may lag
  behind. Based on the current replication protocol, the DAOS servers cannot
  detect the missing I/O requests, so DAOS cannot guarantee data consistency
  between replicas. The upper layer stack has to either re-submit the
  interrupted I/O requests to enforce data consistency between replicas, or
  abort the epoch and rollback to the consistent status of the container.
  For example, if an application linked to the DAOS client library crashed, then
  it should either replay uncompleted I/O requests if it has log for those I/Os,
  or abort the uncommitted epoch.

#### Server replication

DAOS also supports server replication, which has stronger consistency of
replicas with a trade-off in performance and latency. In server replication mode
DAOS client selects a leader shard to send the IO request with the need-to-
forward shards embedded in the RPC request, when the leader shard gets that IO
request it handles it as below steps:
- firstly forward the IO request to others shards
  For the request forwarding, it is offload to the vos target's offload extream
  to release the main IO service extream from IO request sending and reply
  receiving (see shard_req_forward).
- then serve the IO request locally
- wait the forwarded IO's completion and reply client IO request.

In this mode the conflict writes can be detected and serialized by the leader
shard server. Now both modes are supported by DAOS, it can be dynamically
configured by environment variable "DAOS_IO_SRV_DISPATCH" before loading DAOS
server. By default DAOS works in server replication mode, and if the ENV set as
zero then will work in client replication mode.

### Erasure Code

In the case of replicating a whole object, the storage overhead would be 100%
for each replica. This is unaffordable in some cases, so DAOS also provides
erasure code as another option of data protection, with better storage
efficiency.

Erasure codes may be used to improve resilience, with low space overhead. This
feature is still working in progress.

## Object data distribution strategy

DAOS supports different data distribution strategies.

### Single (unstriped) Object (DAOS_OS_SINGLE)

Single (unstriped) objects always has one stripe and each shard of it is a full
replica, they can generate the localities of replicas by the placement
algorithm. A single (unstriped) object can be either a byte-array or a KV.

### Fixed Stripe Object (DAOS_OS_STRIPED)

A fixed stripe object has a constant number of stripes and each stripe has a
fixed stripe size. Upper levels provide values for these attributes when
initializing the stripe schema, and then DAOS uses these attributes to compute
object layout.

### Dynamically Striped Object

A fixed stripe object always has the same number of stripes since it was
created. In contrast, a dynamically stripped object could be created with a
single stripe. It will increase its stripe count as its size grows to some
boundary, to achieve more storage space and better concurrent I/O performance.

Now the dynamically Striped Object schema defined in DAOS (DAOS_OS_DYN_STRIPED/
DAOS_OS_DYN_CHUNKED) but not implemented yet.
