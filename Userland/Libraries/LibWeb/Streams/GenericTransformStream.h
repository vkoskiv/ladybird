#pragma once

#include <AK/Forward.h>
#include <LibJS/Forward.h>
#include <LibWeb/Streams/TransformStream.h>

namespace Web::Streams {

class GenericTransformStreamMixin {

public:
    virtual ~GenericTransformStreamMixin() = default;

    JS::NonnullGCPtr<ReadableStream> readable() { return m_stream->readable(); }
    JS::NonnullGCPtr<WritableStream> writable() { return m_stream->writable(); }

protected:
    explicit GenericTransformStreamMixin();
    void visit_edges(JS::Cell::Visitor&);
    JS::GCPtr<TransformStream> m_stream;
};

}
