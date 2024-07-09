#pragma once

#include <AK/Forward.h>
#include <LibJS/Forward.h>
#include <LibWeb/Streams/TransformStream.h>

namespace Web::Streams {

class GenericTransformStreamMixin {

public:
    virtual ~GenericTransformStreamMixin() = default;

    GC::Ref<ReadableStream> readable() { return m_stream->readable(); }
    GC::Ref<WritableStream> writable() { return m_stream->writable(); }

protected:
    explicit GenericTransformStreamMixin();
    void visit_edges(JS::Cell::Visitor&);
    GC::Ptr<TransformStream> m_stream;
};

}
