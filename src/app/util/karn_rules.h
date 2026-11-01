#ifndef APP_UTIL_KARN_RULES_H_INCLUDED
#define APP_UTIL_KARN_RULES_H_INCLUDED
#pragma once

#include "doc/object_id.h"
#include "doc/object_version.h"
#include <map>
#include <string>

namespace doc {
class Sprite;
}
namespace app {
class Tx;
using KarnImageVersions = std::map<doc::ObjectId, doc::ObjectVersion>;
KarnImageVersions karn_image_versions(const doc::Sprite* sprite);
// Empty means compatible. Unchanged images need not be scanned again during editing.
std::string karn_rules_violation(const doc::Sprite* sprite,
                                 const KarnImageVersions& unchanged = {});
void apply_karn_rules(Tx& tx, doc::Sprite* sprite);
} // namespace app
#endif
