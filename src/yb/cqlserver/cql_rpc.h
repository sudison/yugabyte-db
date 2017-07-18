//
// Copyright (c) YugaByte, Inc.
//
#ifndef YB_CQLSERVER_CQL_RPC_H
#define YB_CQLSERVER_CQL_RPC_H

#include "yb/cqlserver/cql_message.h"

#include "yb/rpc/connection.h"
#include "yb/rpc/rpc_with_call_id.h"
#include "yb/rpc/server_event.h"

#include "yb/sql/sql_session.h"

namespace yb {
namespace cqlserver {

class CQLConnectionContext : public rpc::ConnectionContextWithCallId {
 public:
  CQLConnectionContext();

 private:
  uint64_t ExtractCallId(rpc::InboundCall* call) override;
  void RunNegotiation(rpc::ConnectionPtr connection, const MonoTime& deadline) override;
  CHECKED_STATUS ProcessCalls(const rpc::ConnectionPtr& connection,
                              Slice slice,
                              size_t* consumed) override;
  size_t BufferLimit() override;

  CHECKED_STATUS HandleInboundCall(const rpc::ConnectionPtr& connection, Slice slice);

  // SQL session of this CQL client connection.
  // TODO(robert): To get around the need for this RPC layer to link with the SQL layer for the
  // reference to the SqlSession here, the whole SqlSession definition is contained in sql_session.h
  // and #include'd in connection.h/.cc. When SqlSession gets more complicated (say when we support
  // Cassandra ROLE), consider adding a CreateNewConnection method in rpc::ServiceIf so that
  // CQLConnection can be created and returned from CQLServiceImpl.CreateNewConnection().
  sql::SqlSession::SharedPtr sql_session_;
};

class CQLInboundCall : public rpc::InboundCall {
 public:
  explicit CQLInboundCall(rpc::ConnectionPtr conn,
                          CallProcessedListener call_processed_listener,
                          sql::SqlSession::SharedPtr sql_session);

  CHECKED_STATUS ParseFrom(Slice source);

  // Serialize the response packet for the finished call.
  // The resulting slices refer to memory in this object.
  void Serialize(std::deque<util::RefCntBuffer>* output) const override;

  void LogTrace() const override;
  std::string ToString() const override;
  void DumpPB(const rpc::DumpRunningRpcsRequestPB& req, rpc::RpcCallInProgressPB* resp) override;

  MonoTime GetClientDeadline() const override;

  // Return the response message buffer.
  util::RefCntBuffer& response_msg_buf() {
    return response_msg_buf_;
  }

  // Return the SQL session of this CQL call.
  const sql::SqlSession::SharedPtr& sql_session() const {
    return sql_session_;
  }

  void SetResumeFrom(Callback<void(void)>* resume_from) {
    resume_from_ = resume_from;
  }

  bool TryResume();

  uint16_t stream_id() const { return stream_id_; }

  const std::string& service_name() const override;
  const std::string& method_name() const override;
  void RespondFailure(rpc::ErrorStatusPB::RpcErrorCodePB error_code, const Status& status) override;
  void RespondSuccess(const util::RefCntBuffer& buffer, const yb::rpc::RpcMethodMetrics& metrics);

 private:
  void RecordHandlingStarted(scoped_refptr<Histogram> incoming_queue_time) override;

  Callback<void(void)>* resume_from_ = nullptr;
  util::RefCntBuffer response_msg_buf_;
  sql::SqlSession::SharedPtr sql_session_;
  uint16_t stream_id_;
};

} // namespace cqlserver
} // namespace yb

#endif // YB_CQLSERVER_CQL_RPC_H