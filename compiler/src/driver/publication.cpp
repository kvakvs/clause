#include "publication.hpp"
#include "../artifacts/artifacts.hpp"

namespace clause::cli {
void publish(const Publication &pending) {
    artifacts::publish(pending.result.outputs(), pending.root, pending.inputs, pending.object_extension);
}
} // namespace clause::cli
