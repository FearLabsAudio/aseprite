// Verify the emitted files with KarnEdit's actual reader, owner store, and
// collage resolver. Only the isolated Aseprite test fixture is read/written.
using System.Reflection;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using KarnEdit2.Editor.TileKits;

if (args.Length < 2 || args.Length > 3)
    throw new ArgumentException("Usage: KarnEditContract <fixture-root> <KarnEdit-binary-folder> [collage-merge-fixture-root]");
AssemblyLoadContext.Default.Resolving += (_, name) =>
{
    string path = Path.Combine(args[1], name.Name + ".dll");
    return File.Exists(path) ? AssemblyLoadContext.Default.LoadFromAssemblyPath(Path.GetFullPath(path)) : null;
};
static void Check(bool condition, string message)
{
    if (!condition) throw new Exception(message);
}
string root = Path.GetFullPath(args[0]);
using JsonDocument manifest = JsonDocument.Parse(File.ReadAllText(Path.Combine(root, "manifest.json")));
string Value(string key) => manifest.RootElement.GetProperty(key).GetString()!;
foreach (var example in manifest.RootElement.GetProperty("identityCases").EnumerateArray())
{
    string identity = "1\n" + example.GetProperty("path").GetString()!.ToUpperInvariant();
    string expected = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(identity)).AsSpan(0,16)).ToLowerInvariant();
    Check(example.GetProperty("id").GetString() == expected, "Invariant source path case, including Unicode.");
}
var store = new TileKitStore(Value("tilekits"), Value("workspace"));
store.Reload();
Check(store.ReloadWarnings.Count == 0, string.Join("\n", store.ReloadWarnings));
Check(store.Kits.Count == 3, "Expected two bitmap kits and a collage.");
TileKit collage = store.Kits.Single(k => k.Kind == TileKitKind.Collage);
Check(collage.Collage!.Width == 16 && collage.Collage.Height == 16, "Collage dimensions.");
foreach (TileKit kit in store.Kits.Where(k => k.Kind == TileKitKind.Bitmap))
{
    string identity = "1\n" + kit.ImagePath.Replace('\\', '/').TrimStart('/').ToUpperInvariant();
    string expected = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(identity)).AsSpan(0, 16)).ToLowerInvariant();
    Check(kit.SourceFileId == expected, "Deterministic source identity: " + kit.ImagePath);
    Check(File.Exists(store.GetImagePathAbs(kit)), "Workspace-relative source resolves.");
}
var trees = BmpIndexedLoader.Load(Value("trees"));
Check(trees.BitsPerPixel == 4 && trees.Width == 24 && trees.Height == 16, "BMP geometry.");
Check(trees.PixelsIndexed[0] == 5 && trees.PixelsIndexed[1] == 6, "Equal-RGB indices remain distinct.");
Check(trees.PixelsIndexed[2] == 0, "Upper transparent holes must remain transparent.");
Check(trees.UsesReservedIndexes1Or2 && trees.PixelsIndexed[8] == 0, "KarnEdit reserved-index import rule.");
Check(trees.PaletteRgb24[5] == trees.PaletteRgb24[6], "Duplicate palette slots.");
Check(store.Kits.Any(k => k.Brushes.Any(b => b.Id == "brush-one")), "Existing brushes preserved.");
// Resolve the collage through the same code used when KarnEdit opens it.
Type type = typeof(TileKitStore).Assembly.GetType("KarnEdit2.Editor.MapEditor.MapEditorPanel", true)!;
MethodInfo resolve = type.GetMethod("BuildMapDocumentFromCollage", BindingFlags.NonPublic | BindingFlags.Static)!;
object?[] parameters = [collage, store, null];
object doc = resolve.Invoke(null, parameters)!;
Check(((System.Collections.ICollection)parameters[2]!).Count == 0, "No broken collage source references.");
var layer = doc.GetType().GetProperty("L1")!.GetValue(doc)!;
var indices = (int[])layer.GetType().GetProperty("TileImageIndices")!.GetValue(layer)!;
var images = (System.Collections.IList)doc.GetType().GetProperty("TileImages")!.GetValue(doc)!;
byte[] Pixels(int cell)
{
    object image = images[indices[cell]]!;
    return (byte[])image.GetType().GetProperty("Pixels4Bpp")!.GetValue(image)!;
}
Check(Pixels(0)[0] == 5 && Pixels(0)[1] == 6 && Pixels(0)[2] == 0, "Top tile wins, including transparent holes.");
Check(Pixels(2)[0] == 3, "Empty upper tile reveals lower tile.");
if (args.Length == 3)
{
    using JsonDocument merge = JsonDocument.Parse(File.ReadAllText(Path.Combine(args[2], "manifest.json")));
    var mergeRoot = merge.RootElement;
    var mergeStore = new TileKitStore(mergeRoot.GetProperty("tilekits").GetString()!,
                                    mergeRoot.GetProperty("workspace").GetString()!);
    mergeStore.Reload();
    Check(mergeStore.ReloadWarnings.Count == 0, "Merged collage sources load cleanly.");
    var mergeCollage = mergeStore.Kits.Single(k => k.Kind == TileKitKind.Collage);
    object?[] mergeParameters = [mergeCollage, mergeStore, null];
    object mergeDoc = resolve.Invoke(null, mergeParameters)!;
    Check(((System.Collections.ICollection)mergeParameters[2]!).Count == 0, "Merged source references resolve.");
    var mergeLayer = mergeDoc.GetType().GetProperty("L1")!.GetValue(mergeDoc)!;
    var mergeIndices = (int[])mergeLayer.GetType().GetProperty("TileImageIndices")!.GetValue(mergeLayer)!;
    var mergeImages = (System.Collections.IList)mergeDoc.GetType().GetProperty("TileImages")!.GetValue(mergeDoc)!;
    foreach (var check in mergeRoot.GetProperty("checks").EnumerateArray())
    {
        int cell = check.GetProperty("cell").GetInt32();
        object tile = mergeImages[mergeIndices[cell]]!;
        byte[] pixels = (byte[])tile.GetType().GetProperty("Pixels4Bpp")!.GetValue(tile)!;
        int offset = 0;
        foreach (var expected in check.GetProperty("pixels").EnumerateArray())
            Check(pixels[offset++] == expected.GetInt32(), $"Merged collage cell {cell} exact indices.");
        var sourceCell = mergeCollage.Collage!.Cells.Single(c => c.X == cell % 16 && c.Y == cell / 16);
        var sourceKit = mergeStore.Kits.Single(k => k.Id == sourceCell.TileKitId);
        var sourceBmp = BmpIndexedLoader.Load(mergeStore.GetImagePathAbs(sourceKit));
        Check(sourceBmp.PaletteRgb24[0] == check.GetProperty("bank").GetInt32() * 17 * 256,
              $"Merged collage cell {cell} uses only its winning bank.");
    }
    Console.WriteLine("KarnEdit merged collage pixels and winning palettes: PASS");
}
Console.WriteLine("KarnEdit native BMP reader, TileKit store, source identity and collage resolver: PASS");
