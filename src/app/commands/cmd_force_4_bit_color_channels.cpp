// Aseprite
// Distributed under the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/cmd/set_palette.h"
#include "app/cmd/set_user_data.h"
#include "app/commands/command.h"
#include "app/commands/params.h"
#include "app/context_access.h"
#include "app/i18n/strings.h"
#include "app/tx.h"
#include "doc/color.h"
#include "doc/palette.h"
#include "doc/palette_rgb444.h"

namespace app {

class Force4BitColorChannelsCommand : public Command {
public:
  Force4BitColorChannelsCommand() : Command(CommandId::Force4BitColorChannels()) {}

protected:
  void onLoadParams(const Params& params) override
  {
    m_enabled = params.has_param("enabled") ? (params.get_as<bool>("enabled") ? 1 : 0) : -1;
  }

  bool onChecked(Context* context) override
  {
    return doc::force_rgb444(context->activeSite().sprite());
  }

  bool onEnabled(Context* context) override
  {
    const auto site = context->activeSite();
    return site.sprite() && site.sprite()->pixelFormat() == IMAGE_INDEXED &&
           context->checkFlags(ContextFlags::ActiveDocumentIsWritable);
  }

  void onExecute(Context* context) override
  {
    ContextWriter writer(context);
    auto* sprite = writer.sprite();
    const bool previous = doc::force_rgb444(sprite);
    const bool enabled = m_enabled < 0 ? !previous : m_enabled != 0;
    if (enabled == previous)
      return;

    Tx tx(writer, Strings::commands_Force4BitColorChannels(), ModifyDocument);
    auto data = sprite->userData();
    data.properties(doc::kRgb444Properties)["enabled"] = enabled;
    tx(new cmd::SetUserData(sprite, data, writer.document()));
    if (enabled) {
      // Include all banks and animation palettes. Disabling retains colors.
      for (const auto* original : sprite->getPalettes()) {
        doc::Palette palette(*original);
        doc::snap_rgb444(palette);
        if (palette.countDiff(original, nullptr, nullptr))
          tx(new cmd::SetPalette(sprite, original->frame(), &palette));
      }
    }
    tx.commit();
  }

private:
  int m_enabled = -1;
};

Command* CommandFactory::createForce4BitColorChannelsCommand()
{
  return new Force4BitColorChannelsCommand;
}

} // namespace app
