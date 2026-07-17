#pragma once

#include <arpa/inet.h>

#include <QByteArray>
#include <QIODevice>
#include <QtProtobuf/qabstractprotobufserializer.h>
#include <QtProtobuf/qprotobufserializer.h>

#include <array>
#include <cstring>
#include <stdexcept>

namespace onerss::desktop {

template <typename Stream, typename Envelope>
Envelope readEnvelope(Stream &stream) {
  std::array<std::byte, 4> size_buffer{};
  char *size_data = reinterpret_cast<char *>(size_buffer.data());
  qsizetype size_read = 0;
  while (size_read < static_cast<qsizetype>(size_buffer.size())) {
    const auto bytes_read = stream.read(size_data + size_read, static_cast<qint64>(size_buffer.size() - size_read));
    if (bytes_read > 0) {
      size_read += bytes_read;
      continue;
    }
    if (!stream.waitForReadyRead(30000)) {
      throw std::runtime_error{"failed to read protobuf frame header"};
    }
  }
  std::uint32_t frame_size_network = 0;
  std::memcpy(&frame_size_network, size_buffer.data(), sizeof(frame_size_network));
  const auto frame_size = ntohl(frame_size_network);
  if (frame_size == 0 || frame_size > 1024 * 1024) {
    throw std::runtime_error{"received invalid frame size from server"};
  }

  QByteArray payload(static_cast<qsizetype>(frame_size), Qt::Uninitialized);
  qsizetype payload_read = 0;
  while (payload_read < payload.size()) {
    const auto bytes_read = stream.read(payload.data() + payload_read, payload.size() - payload_read);
    if (bytes_read > 0) {
      payload_read += bytes_read;
      continue;
    }
    if (!stream.waitForReadyRead(30000)) {
      throw std::runtime_error{"failed to read protobuf payload"};
    }
  }
  Envelope envelope;
  QProtobufSerializer serializer;
  if (!envelope.deserialize(&serializer, payload)) {
    throw std::runtime_error{"failed to parse protobuf envelope"};
  }
  return envelope;
}

template <typename Stream, typename Envelope>
void writeEnvelope(Stream &stream, const Envelope &envelope) {
  QProtobufSerializer serializer;
  const QByteArray payload = envelope.serialize(&serializer);
  if (serializer.lastError() != QAbstractProtobufSerializer::Error::None) {
    throw std::runtime_error{"failed to serialize protobuf envelope"};
  }
  if (payload.size() > 1024 * 1024) {
    throw std::runtime_error{"protobuf envelope exceeds maximum size"};
  }

  const auto frame_size = htonl(static_cast<std::uint32_t>(payload.size()));
  const char *frame_data = reinterpret_cast<const char *>(&frame_size);
  qint64 written = 0;
  while (written < static_cast<qint64>(sizeof(frame_size))) {
    const auto bytes_written = stream.write(frame_data + written, static_cast<qint64>(sizeof(frame_size)) - written);
    if (bytes_written < 0) {
      throw std::runtime_error{"failed to write protobuf frame header"};
    }
    written += bytes_written;
    if (!stream.waitForBytesWritten(30000)) {
      throw std::runtime_error{"failed to flush protobuf frame header"};
    }
  }

  written = 0;
  while (written < payload.size()) {
    const auto bytes_written = stream.write(payload.constData() + written, payload.size() - written);
    if (bytes_written < 0) {
      throw std::runtime_error{"failed to write protobuf payload"};
    }
    written += bytes_written;
    if (!stream.waitForBytesWritten(30000)) {
      throw std::runtime_error{"failed to flush protobuf payload"};
    }
  }
}

}  // namespace onerss::desktop
