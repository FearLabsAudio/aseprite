#include "app/cmd/set_user_data.h"
#include "app/commands/command.h"
#include "app/commands/params.h"
#include "app/context_access.h"
#include "app/modules/gui.h"
#include "app/tx.h"
#include "app/util/karn_rules.h"
#include "doc/karn_rules.h"
#include "doc/palette_rgb444.h"
#include "ui/alert.h"

namespace app {
class ForceKarnRulesCommand : public Command {
public:
  ForceKarnRulesCommand() : Command(CommandId::ForceKarnRules()) {}

protected:
  void onLoadParams(const Params& params) override
  {
    m_enabled = params.has_param("enabled") ? (params.get_as<bool>("enabled") ? 1 : 0) : -1;
    m_ui = !params.has_param("ui") || params.get_as<bool>("ui");
  }
  bool onChecked(Context* context) override
  {
    return doc::force_karn_rules(context->activeSite().sprite());
  }
  bool onEnabled(Context* context) override
  {
    return context->activeSite().sprite() &&
           context->checkFlags(ContextFlags::ActiveDocumentIsWritable);
  }
  void onExecute(Context* context) override
  {
    auto* s = context->activeSite().sprite();
    const bool previous = doc::force_karn_rules(s);
    const bool enabled = m_enabled < 0 ? !previous : m_enabled != 0;
    if (enabled == previous)
      return;
    if (enabled && !karn_rules_violation(s).empty() && context->isUIAvailable() && m_ui) {
      if (ui::Alert::show("Force KARN rules<<KARN rules enforce"
                          "<<8 x 8 pixel tiles; horizontal and vertical flips (no diagonal)."
                          "<<4-bit per channel palette colors."
                          "<<Indexed color; local indices 0-15 per layer; index 0 transparent."
                          "<<Palette banks and unbanked layers are allowed."
                          "<<"
                          "<<Incompatible tiles are rebuilt; excess colors are reduced."
                          "<<"
                          "<<Apply these settings now?||&Apply||&Cancel") != 1)
        return;
    }
    ContextWriter writer(context);
    Tx tx(writer, "Force KARN rules");
    if (enabled)
      apply_karn_rules(tx, s);
    auto data = s->userData();
    data.properties(doc::kKarnRulesProperties)["enabled"] = enabled;
    if (enabled)
      data.properties(doc::kRgb444Properties)["enabled"] = true;
    tx(new cmd::SetUserData(s, data, writer.document()));
    tx.commit();
    update_screen_for_document(writer.document());
  }

private:
  int m_enabled = -1;
  bool m_ui = true;
};
Command* CommandFactory::createForceKarnRulesCommand()
{
  return new ForceKarnRulesCommand;
}
} // namespace app
