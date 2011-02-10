/*
Copyright (C) 2010 Srivats P.

This file is part of "Ostinato"

This is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>
*/

#include "pdml_p.h"

#include "mac.pb.h"
#include "eth2.pb.h"
#include "hexdump.pb.h"
#include "ip4.pb.h"
#include "ip6.pb.h"
#include "tcp.pb.h"

#include <google/protobuf/descriptor.h>

#include <QMessageBox>

#include <string>

const int kBaseHex = 16;

PdmlDefaultProtocol::PdmlDefaultProtocol()
{
    ostProtoId_ = -1;
}

PdmlDefaultProtocol::~PdmlDefaultProtocol()
{
}

QString PdmlDefaultProtocol::pdmlProtoName() const
{
    return pdmlProtoName_;
}

int PdmlDefaultProtocol::ostProtoId() const
{
    return ostProtoId_;
}

bool PdmlDefaultProtocol::hasField(QString name) const
{
    return fieldMap_.contains(name);
}

int PdmlDefaultProtocol::fieldId(QString name) const
{
    return fieldMap_.value(name);
}

void PdmlDefaultProtocol::preProtocolHandler(QString name, 
        const QXmlAttributes &attributes, OstProto::Stream *stream)
{
    return; // do nothing!
}

void PdmlDefaultProtocol::postProtocolHandler(OstProto::Stream *stream)
{
    return; // do nothing!
}

void PdmlDefaultProtocol::unknownFieldHandler(QString name, 
        int pos, int size, const QXmlAttributes &attributes, 
        OstProto::Stream *stream)
{
    return; // do nothing!
}


// ---------------------------------------------------------- //
// PdmlParser
// ---------------------------------------------------------- //
PdmlParser::PdmlParser(OstProto::StreamConfigList *streams) 
{ 
    skipCount_ = 0;
    currentPdmlProtocol_ = NULL;

    streams_ =  streams;

    protocolMap_.insert("unknown", new PdmlUnknownProtocol());
    protocolMap_.insert("geninfo", new PdmlGenInfoProtocol());
    protocolMap_.insert("frame", new PdmlFrameProtocol());
#if 0
    protocolMap_.insert("fake-field-wrapper", 
            new PdmlFakeFieldWrapperProtocol());
#endif
#if 0
    protocolMap_.insert("eth", new PdmlEthProtocol());
    protocolMap_.insert("ip", new PdmlIp4Protocol());
    protocolMap_.insert("ipv6", new PdmlIp6Protocol());
    protocolMap_.insert("tcp", new PdmlTcpProtocol());
#endif
}

PdmlParser::~PdmlParser()
{
    // TODO: free protocolMap_.values()
}

bool PdmlParser::startElement(const QString & /* namespaceURI */,
    const QString & /* localName */,
    const QString &qName,
    const QXmlAttributes &attributes)
{
    qDebug("%s (%s)", __FUNCTION__, qName.toAscii().constData());

    if (skipCount_)
    {
        skipCount_++;
        goto _exit;
    }

    if (qName == "pdml") 
    {
        packetCount_ = 0;
    }
    else if (qName == "packet") 
    {
        // XXX: For now, each packet is converted to a stream
        currentStream_ = streams_->add_stream();
        currentStream_->mutable_stream_id()->set_id(packetCount_);
        currentStream_->mutable_core()->set_is_enabled(true);

        qDebug("packetCount_ = %d\n", packetCount_);
    }
    else if (qName == "proto")
    {
        QString protoName = attributes.value("name");
        if (protoName.isEmpty()
            || (protoName == "expert"))
        {
            skipCount_++;
            goto _exit;
        }

        // HACK HACK HACK 
        if (currentPdmlProtocol_ && (currentPdmlProtocol_->ostProtoId() 
                == OstProto::Protocol::kHexDumpFieldNumber))
            currentPdmlProtocol_->postProtocolHandler(currentStream_);


        if (!protocolMap_.contains(protoName))
            protoName = "unknown"; // FIXME: change to Ost:Hexdump

        currentPdmlProtocol_ = protocolMap_.value(protoName);

        Q_ASSERT(currentPdmlProtocol_ != NULL);

        int protoId = currentPdmlProtocol_->ostProtoId();

        // PdmlDefaultProtocol =>
        if (protoId <= 0)
            goto _exit;

        OstProto::Protocol *proto = currentStream_->add_protocol();

        proto->mutable_protocol_id()->set_id(protoId);

        const google::protobuf::Reflection *msgRefl = 
            proto->GetReflection();
        const google::protobuf::FieldDescriptor *fDesc = 
            msgRefl->FindKnownExtensionByNumber(protoId);

        // TODO: if !fDesc
        // init default values of all fields in protocol
        currentProtocolMsg_ = msgRefl->MutableMessage(proto, fDesc);

        currentPdmlProtocol_->preProtocolHandler(protoName, attributes,
                currentStream_);

    }
    else if (qName == "field")
    {
        // fields with "hide='yes'" are informational and should be skipped
        if (attributes.value("hide") == "yes")
        {
            skipCount_++;
            goto _exit;
        }

        QString name = attributes.value("name");
        QString valueStr = attributes.value("value");
        int pos = -1;
        int size = -1;

        if (!attributes.value("pos").isEmpty())
            pos = attributes.value("pos").toInt();
        if (!attributes.value("size").isEmpty())
            size = attributes.value("size").toInt();

        qDebug("\tname:%s, pos:%d, size:%d value:%s",
                name.toAscii().constData(), 
                pos, 
                size, 
                valueStr.toAscii().constData());

        if (!currentPdmlProtocol_->hasField(name))
        {
            currentPdmlProtocol_->unknownFieldHandler(name, pos, size, 
                    attributes, currentStream_);
            goto _exit;
        }

        // TODO
        int fId = currentPdmlProtocol_->fieldId(name);
        const google::protobuf::Descriptor *msgDesc = 
            currentProtocolMsg_->GetDescriptor();
        const google::protobuf::FieldDescriptor *fDesc = 
            msgDesc->FindFieldByNumber(fId);
        const google::protobuf::Reflection *msgRefl = 
            currentProtocolMsg_->GetReflection();

        bool isOk;

        switch(fDesc->cpp_type())
        {
        case google::protobuf::FieldDescriptor::CPPTYPE_ENUM: // TODO
        case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
            msgRefl->SetUInt32(currentProtocolMsg_, fDesc, 
                    valueStr.toUInt(&isOk, kBaseHex));
            break;
        case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
            msgRefl->SetUInt64(currentProtocolMsg_, fDesc, 
                    valueStr.toULongLong(&isOk, kBaseHex));
            break;
        case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
        {
            QByteArray hexVal = QByteArray::fromHex(valueStr.toUtf8());
            std::string str(hexVal.constData(), hexVal.size());
            msgRefl->SetString(currentProtocolMsg_, fDesc, str);
            break;
        }
        default:
            qDebug("%s: unhandled cpptype = %d", __FUNCTION__, 
                    fDesc->cpp_type());
        }
    }

_exit:
    return true;

}

bool PdmlParser::characters(const QString &str)
{
    //qDebug("%s (%s)", __FUNCTION__, str.toAscii().constData());
    //_currentText += str;
    return true;
}

bool PdmlParser::endElement(const QString & /* namespaceURI */,
                            const QString & /* localName */,
                            const QString &qName)
{
    qDebug("%s (%s)", __FUNCTION__, qName.toAscii().constData());

    if (qName == "packet") 
    {
        if (currentStream_->core().name().size())
        {
            OstProto::Protocol *proto = currentStream_->add_protocol();

            proto->mutable_protocol_id()->set_id(
                    OstProto::Protocol::kHexDumpFieldNumber);

            OstProto::HexDump *hexDump = proto->MutableExtension(OstProto::hexDump);

            hexDump->set_content(currentStream_->core().name());
            hexDump->set_pad_until_end(false);
            currentStream_->mutable_core()->set_name("");
        }
        packetCount_++;
        currentPdmlProtocol_ = NULL;
        goto _exit;
    }

    if (skipCount_)
    {
        skipCount_--;
        goto _exit;
    }

    if (qName == "proto")
    {
        currentPdmlProtocol_->postProtocolHandler(currentStream_);
        //currentPdmlProtocol_ = NULL;
    }
    else if (qName == "field")
    {
    }

_exit:
    return true;
}

bool PdmlParser::fatalError(const QXmlParseException &exception)
{
    QString extra;

    qDebug("%s", __FUNCTION__);
#if 0
    if (exception.message() == "tag mismatch" && lastElement == "fieldData")
        extra = "\nAre you using an old version of Wireshark? If so, try using a newer version. Alternatively, view the packet dump decode in Wireshark by clicking the \"External\" button.";
#endif

    QMessageBox::warning(0, QObject::tr("PDML Parser"),
                         QObject::tr("XML parse error for packet %1 "
                            "at line %2, column %3:\n    %4\n%5")
                         .arg(packetCount_+1)
                         .arg(exception.lineNumber())
                         .arg(exception.columnNumber())
                         .arg(exception.message())
                         .arg(extra));
    return false;
}


// ---------------------------------------------------------- //
// PdmlUnknownProtocol                                        //
// ---------------------------------------------------------- //

PdmlUnknownProtocol::PdmlUnknownProtocol()
{
    pdmlProtoName_ = "OST:HexDump";
    ostProtoId_ = OstProto::Protocol::kHexDumpFieldNumber;

    endPos_ = expPos_ = -1;
}

void PdmlUnknownProtocol::preProtocolHandler(QString name, 
        const QXmlAttributes &attributes, OstProto::Stream *stream)
{
    bool isOk;
    int size;
    int pos = attributes.value("pos").toUInt(&isOk);
    if (!isOk)
        goto _skip_pos_size_proc;

    size = attributes.value("size").toUInt(&isOk);
    if (!isOk)
        goto _skip_pos_size_proc;

    expPos_ = pos;
    endPos_ = expPos_ + size;

_skip_pos_size_proc:
    OstProto::HexDump *hexDump = stream->mutable_protocol(
            stream->protocol_size()-1)->MutableExtension(OstProto::hexDump);
    hexDump->set_pad_until_end(false);
}

void PdmlUnknownProtocol::postProtocolHandler(OstProto::Stream *stream)
{
    OstProto::HexDump *hexDump = stream->mutable_protocol(
            stream->protocol_size()-1)->MutableExtension(OstProto::hexDump);

    // Skipped field(s) at end? Pad with zero!
    if (endPos_ > expPos_)
    {
        QByteArray hexVal(endPos_ - expPos_, char(0));

        hexDump->mutable_content()->append(hexVal.constData(), hexVal.size());
        expPos_ += hexVal.size();
    }

    qDebug("%s: expPos_ = %d, endPos_ = %d\n", __FUNCTION__, expPos_, endPos_); 
    //Q_ASSERT(expPos_ == endPos_);

    hexDump->set_pad_until_end(false);
    endPos_ = expPos_ = -1;
}

void PdmlUnknownProtocol::unknownFieldHandler(QString name, int pos, int size, 
            const QXmlAttributes &attributes, OstProto::Stream *stream)
{
    OstProto::HexDump *hexDump = stream->mutable_protocol(
            stream->protocol_size()-1)->MutableExtension(OstProto::hexDump);

    qDebug("%s: %s, pos = %d, expPos_ = %d, endPos_ = %d\n", 
            __PRETTY_FUNCTION__, name.toAscii().constData(), 
            pos, expPos_, endPos_); 

    // Skipped field? Pad with zero!
    if ((pos > expPos_) && (expPos_ < endPos_))
    {
        QByteArray hexVal(pos - expPos_, char(0));

        hexDump->mutable_content()->append(hexVal.constData(), hexVal.size());
        expPos_ += hexVal.size();
    }

    if ((pos == expPos_) /*&& (pos < endPos_)*/)
    {
        QByteArray hexVal = attributes.value("unmaskedvalue").isEmpty() ?
                QByteArray::fromHex(attributes.value("value").toUtf8()) :
                QByteArray::fromHex(attributes.value("unmaskedvalue").toUtf8());

        hexDump->mutable_content()->append(hexVal.constData(), hexVal.size());
        expPos_ += hexVal.size();
    }
}


// ---------------------------------------------------------- //
// PdmlGenInfoProtocol                                        //
// ---------------------------------------------------------- //

PdmlGenInfoProtocol::PdmlGenInfoProtocol()
{
    pdmlProtoName_ = "geninfo";
}

void PdmlGenInfoProtocol::unknownFieldHandler(QString name, int pos, 
        int size, const QXmlAttributes &attributes, OstProto::Stream *stream)
{
    stream->mutable_core()->set_frame_len(size+4); // TODO:check FCS
}

// ---------------------------------------------------------- //
// PdmlFrameProtocol                                          //
// ---------------------------------------------------------- //

PdmlFrameProtocol::PdmlFrameProtocol()
{
    pdmlProtoName_ = "frame";
}

#if 1
// ---------------------------------------------------------- //
// PdmlFakeFieldWrapperProtocol                                        //
// ---------------------------------------------------------- //

PdmlFakeFieldWrapperProtocol::PdmlFakeFieldWrapperProtocol()
{
    pdmlProtoName_ = "OST:HexDump";
    ostProtoId_ = OstProto::Protocol::kHexDumpFieldNumber;

    expPos_ = -1;
}

void PdmlFakeFieldWrapperProtocol::preProtocolHandler(QString name, 
        const QXmlAttributes &attributes, OstProto::Stream *stream)
{
    expPos_ = 0;
    OstProto::HexDump *hexDump = stream->mutable_protocol(
            stream->protocol_size()-1)->MutableExtension(OstProto::hexDump);
    hexDump->set_pad_until_end(false);
}

void PdmlFakeFieldWrapperProtocol::postProtocolHandler(OstProto::Stream *stream)
{
    OstProto::HexDump *hexDump = stream->mutable_protocol(
            stream->protocol_size()-1)->MutableExtension(OstProto::hexDump);

    qDebug("%s: expPos_ = %d\n", __FUNCTION__, expPos_); 

    hexDump->set_pad_until_end(false);
    expPos_ = -1;
}

void PdmlFakeFieldWrapperProtocol::unknownFieldHandler(QString name, int pos, 
        int size, const QXmlAttributes &attributes, OstProto::Stream *stream)
{
    OstProto::HexDump *hexDump = stream->mutable_protocol(
            stream->protocol_size()-1)->MutableExtension(OstProto::hexDump);

    if ((pos == expPos_) && (size >= 0) && 
            (!attributes.value("unmaskedvalue").isEmpty() || 
             !attributes.value("value").isEmpty()))
    {
        QByteArray hexVal = attributes.value("unmaskedvalue").isEmpty() ?
                QByteArray::fromHex(attributes.value("value").toUtf8()) :
                QByteArray::fromHex(attributes.value("unmaskedvalue").toUtf8());

        hexDump->mutable_content()->append(hexVal.constData(), hexVal.size());
        expPos_ += hexVal.size();
    }
}

#endif
// ---------------------------------------------------------- //
// PdmlEthProtocol                                            //
// ---------------------------------------------------------- //

PdmlEthProtocol::PdmlEthProtocol()
{
    pdmlProtoName_ = "eth";
    ostProtoId_ = OstProto::Protocol::kMacFieldNumber;

    fieldMap_.insert("eth.dst", OstProto::Mac::kDstMacFieldNumber);
    fieldMap_.insert("eth.src", OstProto::Mac::kSrcMacFieldNumber);
}

void PdmlEthProtocol::unknownFieldHandler(QString name, int pos, int size, 
            const QXmlAttributes &attributes, OstProto::Stream *stream)
{
    if (name == "eth.type")
    {
        OstProto::Protocol *proto = stream->add_protocol();

        proto->mutable_protocol_id()->set_id(
                OstProto::Protocol::kEth2FieldNumber);

        OstProto::Eth2 *eth2 = proto->MutableExtension(OstProto::eth2);

        bool isOk;
        eth2->set_type(attributes.value("value").toUInt(&isOk, kBaseHex));
        eth2->set_is_override_type(true);
    }
}


// ---------------------------------------------------------- //
// PdmlIp4Protocol                                            //
// ---------------------------------------------------------- //

PdmlIp4Protocol::PdmlIp4Protocol()
{
    pdmlProtoName_ = "ip";
    ostProtoId_ = OstProto::Protocol::kIp4FieldNumber;

    fieldMap_.insert("ip.version", 5);
    fieldMap_.insert("ip.dsfield", 6);
    fieldMap_.insert("ip.len", 7);
    fieldMap_.insert("ip.id", 8);
    //fieldMap_.insert("ip.flags", 9);
    fieldMap_.insert("ip.frag_offset", 10);
    fieldMap_.insert("ip.ttl", 11);
    fieldMap_.insert("ip.proto", 12);
    fieldMap_.insert("ip.checksum", 13);
    fieldMap_.insert("ip.src", 14);
    fieldMap_.insert("ip.dst", 18);
}

void PdmlIp4Protocol::unknownFieldHandler(QString name, int pos, int size, 
            const QXmlAttributes &attributes, OstProto::Stream *stream)
{
    bool isOk;

    if (name == "ip.flags")
    {
        OstProto::Ip4 *ip4 = stream->mutable_protocol(
                stream->protocol_size()-1)->MutableExtension(OstProto::ip4);

        ip4->set_flags(attributes.value("value").toUInt(&isOk, kBaseHex) >> 5);
    }
}

void PdmlIp4Protocol::postProtocolHandler(OstProto::Stream *stream)
{
    OstProto::Ip4 *ip4 = stream->mutable_protocol(
            stream->protocol_size()-1)->MutableExtension(OstProto::ip4);

    ip4->set_is_override_ver(true); // FIXME
    ip4->set_is_override_hdrlen(true); // FIXME
    ip4->set_is_override_totlen(true); // FIXME
    ip4->set_is_override_proto(true); // FIXME
    ip4->set_is_override_cksum(true); // FIXME
}

// ---------------------------------------------------------- //
// PdmlIp6Protocol                                            //
// ---------------------------------------------------------- //

PdmlIp6Protocol::PdmlIp6Protocol()
{
    pdmlProtoName_ = "ipv6";
    ostProtoId_ = OstProto::Protocol::kIp6FieldNumber;

    fieldMap_.insert("ipv6.version", OstProto::Ip6::kVersionFieldNumber);
    fieldMap_.insert("ipv6.class", OstProto::Ip6::kTrafficClassFieldNumber);
    fieldMap_.insert("ipv6.flow", OstProto::Ip6::kFlowLabelFieldNumber);
    fieldMap_.insert("ipv6.plen", OstProto::Ip6::kPayloadLengthFieldNumber);
    fieldMap_.insert("ipv6.nxt", OstProto::Ip6::kNextHeaderFieldNumber);
    fieldMap_.insert("ipv6.hlim", OstProto::Ip6::kHopLimitFieldNumber);

    // ipv6.src and ipv6.dst handled as unknown fields
}

void PdmlIp6Protocol::unknownFieldHandler(QString name, int pos, int size, 
            const QXmlAttributes &attributes, OstProto::Stream *stream)
{
    bool isOk;

    if (name == "ipv6.src")
    {
        OstProto::Ip6 *ip6 = stream->mutable_protocol(
                stream->protocol_size()-1)->MutableExtension(OstProto::ip6);
        QString addrHexStr = attributes.value("value");

        ip6->set_src_addr_hi(addrHexStr.left(16).toULongLong(&isOk, kBaseHex));
        ip6->set_src_addr_lo(addrHexStr.right(16).toULongLong(&isOk, kBaseHex));
    }
    else if (name == "ipv6.dst")
    {
        OstProto::Ip6 *ip6 = stream->mutable_protocol(
                stream->protocol_size()-1)->MutableExtension(OstProto::ip6);
        QString addrHexStr = attributes.value("value");

        ip6->set_dst_addr_hi(addrHexStr.left(16).toULongLong(&isOk, kBaseHex));
        ip6->set_dst_addr_lo(addrHexStr.right(16).toULongLong(&isOk, kBaseHex));
    }
}

void PdmlIp6Protocol::postProtocolHandler(OstProto::Stream *stream)
{
    OstProto::Ip6 *ip6 = stream->mutable_protocol(
            stream->protocol_size()-1)->MutableExtension(OstProto::ip6);

    ip6->set_is_override_version(true); // FIXME
    ip6->set_is_override_payload_length(true); // FIXME
    ip6->set_is_override_next_header(true); // FIXME
}

// ---------------------------------------------------------- //
// PdmlTcpProtocol                                            //
// ---------------------------------------------------------- //

PdmlTcpProtocol::PdmlTcpProtocol()
{
    pdmlProtoName_ = "tcp";
    ostProtoId_ = OstProto::Protocol::kTcpFieldNumber;

    fieldMap_.insert("tcp.srcport", OstProto::Tcp::kSrcPortFieldNumber);
    fieldMap_.insert("tcp.dstport", OstProto::Tcp::kDstPortFieldNumber);
    fieldMap_.insert("tcp.seq", OstProto::Tcp::kSeqNumFieldNumber);
    fieldMap_.insert("tcp.ack", OstProto::Tcp::kAckNumFieldNumber);
    fieldMap_.insert("tcp.hdr_len", OstProto::Tcp::kHdrlenRsvdFieldNumber);
    fieldMap_.insert("tcp.flags", OstProto::Tcp::kFlagsFieldNumber);
    fieldMap_.insert("tcp.window_size", OstProto::Tcp::kWindowFieldNumber);
    fieldMap_.insert("tcp.checksum", OstProto::Tcp::kCksumFieldNumber);
    fieldMap_.insert("tcp.urgent_pointer", OstProto::Tcp::kUrgPtrFieldNumber);
}

void PdmlTcpProtocol::unknownFieldHandler(QString name, int pos, int size, 
            const QXmlAttributes &attributes, OstProto::Stream *stream)
{
    if (name == "tcp.options")
        options_ = QByteArray::fromHex(attributes.value("value").toUtf8());
    else if (name == "" 
            && attributes.value("show").startsWith("TCP segment data"))
    {
        segmentData_ = QByteArray::fromHex(attributes.value("value").toUtf8());
        stream->mutable_core()->mutable_name()->append(segmentData_.constData(),
                segmentData_.size());
    }
}

void PdmlTcpProtocol::postProtocolHandler(OstProto::Stream *stream)
{
    OstProto::Tcp *tcp = stream->mutable_protocol(
            stream->protocol_size()-1)->MutableExtension(OstProto::tcp);

    tcp->set_is_override_src_port(true); // FIXME
    tcp->set_is_override_dst_port(true); // FIXME
    tcp->set_is_override_hdrlen(true); // FIXME
    tcp->set_is_override_cksum(true); // FIXME

    if (options_.size())
    {
        OstProto::Protocol *proto = stream->add_protocol();

        proto->mutable_protocol_id()->set_id(
                OstProto::Protocol::kHexDumpFieldNumber);

        OstProto::HexDump *hexDump = proto->MutableExtension(OstProto::hexDump);

        hexDump->mutable_content()->append(options_.constData(), 
                options_.size());
        hexDump->set_pad_until_end(false);
        options_.resize(0);
    }

#if 0
    if (segmentData_.size())
    {
        OstProto::Protocol *proto = stream->add_protocol();

        proto->mutable_protocol_id()->set_id(
                OstProto::Protocol::kHexDumpFieldNumber);

        OstProto::HexDump *hexDump = proto->MutableExtension(OstProto::hexDump);

        hexDump->mutable_content()->append(segmentData_.constData(), 
                segmentData_.size());
        hexDump->set_pad_until_end(false);
        segmentData_.resize(0);
    }
#endif
}

