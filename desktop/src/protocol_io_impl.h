#pragma once

#include <arpa/inet.h>

#include <QByteArray>
#include <QtProtobuf/qabstractprotobufserializer.h>
#include <QtProtobuf/qprotobufserializer.h>

#include <array>
#include <cstring>
#include <stdexcept>

namespace onerss::desktop {

template <typename Stream, typename Envelope>
Envelope readEnvelope(Stream &stream) {
  std::array<std::byte, 4> size_buffer{};
  boost::asio::read(stream, boost::asio::buffer(size_buffer));
  std::uint32_t frame_size_network = 0;
  std::memcpy(&frame_size_network, size_buffer.data(), sizeof(frame_size_network));
  const auto frame_size = ntohl(frame_size_network);
  if (frame_size == 0 || frame_size > 1024 * 1024) {
    throw std::runtime_error{"received invalid frame size from server"};
  }

  QByteArray payload(static_cast<qsizetype>(frame_size), Qt::Uninitialized);
  boost::asio::read(stream, boost::asio::buffer(payload.data(), static_cast<std::size_t>(payload.size())));
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

  const auto frame_size = htonl(static_cast<std::uint32_t>(payload.size()));
  boost::asio::write(stream, boost::asio::buffer(&frame_size, sizeof(frame_size)));
  boost::asio::write(stream, boost::asio::buffer(payload.constData(), static_cast<std::size_t>(payload.size())));
}

}  // namespace onerss::desktop
