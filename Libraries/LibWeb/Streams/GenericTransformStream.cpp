#include <LibWeb/Streams/GenericTransformStream.h>

namespace Web::Streams {

GenericTransformStreamMixin::GenericTransformStreamMixin()
{
}

void GenericTransformStreamMixin::visit_edges(JS::Cell::Visitor& visitor)
{
    visitor.visit(m_stream);
}

}
