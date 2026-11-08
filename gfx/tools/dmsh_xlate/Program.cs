// Original Switch shaders (Maxwell machine code) → SPIR-V (the Ryujinx.Graphics.Shader translator), for route B's offline metallib build.
// Difference from Ryujinx.ShaderTools: the translator's "queries" are answered truthfully rather than always with defaults -
//   texture type / image format / vertex attribute type / fragment output type / compute thread group size come from the job file (given by the PC reflection paired via the mapping table; same slot number = same resource);
//   window origin upper-left (measured from the game: nvnDeviceSetWindowOriginMode); the depth convention comes with each job ("depth", game.json depth_mode):
//   near_is_zero is Metal's own, near_is_minus_w (GL, Tears of the Kingdom) gets the translator's z' = z / 2 + w / 2 before every vertex return.
// Usage: dmsh_xlate <job.json> <output dir>
//   Job: [{ "key", "stage": "vs|ps|cs", "code": machine code file (graphics = 0x50-byte SPH + instructions, compute = instructions),
//           "tex": { "slot": "2d|2darray|cube|cubearray|3d|buffer|1d|1darray|2dms|2dmsarray", ... }, "img": { "slot": { "type", "fmt" } },
//           "attr": { "location": "float|sint|uint" }, "fout": { "target": "float|sint|uint" }, "local": [x, y, z] }]
//   Output: <key>.spv and <key>.res.json (offsets of storage buffer slots in c0, etc.; decoding texture handle → slot number is done in the script that builds the metallib)
using Ryujinx.Common.Memory;
using Ryujinx.Graphics.Shader;
using Ryujinx.Graphics.Shader.Translation;
using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Text.Json.Nodes;

static class SwshXlate
{
    // c[2] layout of the NVN driver the game's shaders were compiled for: byte base and slot count of the texture and image handle tables (8 bytes per
    // slot). It differs between games (manosaba: textures 0x168, images 0x120; Tears of the Kingdom and the games nxnative ports: textures 0x20, images
    // 0x120), so each job carries it ("c2": {"tex": [base, slots], "img": [base, slots]}, from game.json nvn_c2 through dmsh_build.py)
    readonly record struct C2(int TexBase, int TexEnd, int ImgBase, int ImgEnd)
    {
        public static C2 Of(JsonObject job)
        {
            if (job["c2"] is not JsonObject c || c["tex"] is not JsonArray t || c["img"] is not JsonArray i || t.Count != 2 || i.Count != 2) throw new InvalidDataException("job has no c2 layout {\"tex\": [base, slots], \"img\": [base, slots]}");
            return new C2((int)t[0], (int)t[0] + 8 * (int)t[1], (int)i[0], (int)i[0] + 8 * (int)i[1]);
        }
    }

    sealed class Accessor : IGpuAccessor
    {
        readonly byte[] _code; readonly JsonObject _job; int _binding = 1; public readonly List<string> Notes = new();
        readonly byte[] _c1; readonly C2 _c2; readonly bool _nearMinusW;
        // Size queries on an image handle (TXQ immediate handle falling in c[2]'s image table): the instruction carries no dimension. Answered from the job's img table, then from the type of the image with the same handle in the previous translation pass (SUST / SULD carry the dimension)
        readonly Dictionary<int, SamplerType> _imgByWord; public bool ImgQueryDefaulted;
        public Accessor(byte[] code, JsonObject job, Dictionary<int, SamplerType> imgByWord = null) { _code = code; _job = job; _imgByWord = imgByWord; _c1 = job["c1"] is JsonNode h ? Convert.FromHexString((string)h) : Array.Empty<byte>(); _c2 = C2.Of(job);
            _nearMinusW = job["depth"] is JsonNode dm ? (string)dm switch { "near_is_minus_w" => true, "near_is_zero" => false, _ => throw new InvalidDataException($"job depth {(string)dm}: near_is_minus_w or near_is_zero") } : throw new InvalidDataException("job has no depth convention (near_is_minus_w / near_is_zero)"); }
        // c1 constant segment (the 0x100 bytes before the control segment in the shader package): the jump table of indirect jump BRX lives here (LDC c[1]); without it the translator cannot resolve jump targets and control flow goes wrong (particle shaders loop forever)
        public uint ConstantBuffer1Read(int offset) { if (offset >= 0 && offset + 4 <= _c1.Length) return BitConverter.ToUInt32(_c1, offset); Notes.Add($"c1 read out of range +{offset:X} (c1 length {_c1.Length})"); return 0; }
        // All resources go into set 0, each with a distinct binding number (spirv-cross emits them as one argument buffer); the translator's convention of giving the buffer of constant segment number index binding = index+1 is not relied on
        public SetBindingPair CreateConstantBufferBinding(int index) => new(0, _binding++);
        public SetBindingPair CreateImageBinding(int count, bool isBuffer) { int b = _binding; _binding += count; return new(0, b); }
        public SetBindingPair CreateStorageBufferBinding(int index) => new(0, _binding++);
        public SetBindingPair CreateTextureBinding(int count, bool isBuffer) { int b = _binding; _binding += count; return new(0, b); }
        public ReadOnlySpan<ulong> GetCode(ulong address, int minimumSize) => MemoryMarshal.Cast<byte, ulong>(new ReadOnlySpan<byte>(_code)[(int)address..]);
        public int QuerySamplerArrayLengthFromPool() => 32;
        public int QueryTextureArrayLengthFromBuffer(int slot) => 32;
        public int QueryTextureArrayLengthFromPool() => 32;

        string Get(string table, int slot) => _job[table] is JsonObject t && t[slot.ToString()] is JsonNode n ? (n is JsonObject o ? (string)o["type"] : (string)n) : null;
        int TexSlot(int handle, int cbufSlot)
        {
            (int off0, _, _) = TextureHandle.UnpackOffsets(handle);
            int bytes = off0 * 4;
            return (cbufSlot < 0 || (cbufSlot & 0xffff) == 2) && bytes >= _c2.TexBase && bytes < _c2.TexEnd && (bytes - _c2.TexBase) % 8 == 0 ? (bytes - _c2.TexBase) / 8 : -1;
        }
        int ImgSlot(int handle, int cbufSlot)
        {
            (int off0, _, _) = TextureHandle.UnpackOffsets(handle);
            int bytes = off0 * 4;
            return (cbufSlot < 0 || (cbufSlot & 0xffff) == 2) && bytes >= _c2.ImgBase && bytes < _c2.ImgEnd && (bytes - _c2.ImgBase) % 8 == 0 ? (bytes - _c2.ImgBase) / 8 : -1;
        }
        public SamplerType QuerySamplerType(int handle, int cbufSlot = -1)
        {
            int islot = ImgSlot(handle, cbufSlot);
            if (islot >= 0 && Get("img", islot) == null)
            {
                (int w, _, _) = TextureHandle.UnpackOffsets(handle);
                if (_imgByWord != null && _imgByWord.TryGetValue(w, out SamplerType it)) { Notes.Add($"texture handle {handle:X} = image slot {islot}, type from the image with the same handle: {it}"); return it; }
                ImgQueryDefaulted = true; Notes.Add($"texture handle {handle:X} = image slot {islot} without a type, taken as 2D"); return SamplerType.Texture2D;
            }
            if (islot >= 0) return Parse(Get("img", islot));
            int slot = TexSlot(handle, cbufSlot);
            string t = slot >= 0 ? Get("tex", slot) ?? Get("img", slot) : null;
            if (t == null) { Notes.Add($"texture handle {handle:X} (constant bank {cbufSlot}, slot {slot}) has no known type, taken as 2D"); return SamplerType.Texture2D; }
            return Parse(t);
        }
        static SamplerType Parse(string t)
        {
            return t switch
            {
                "1d" => SamplerType.Texture1D, "1darray" => SamplerType.Texture1D | SamplerType.Array,
                "buffer" => SamplerType.TextureBuffer, "2d" => SamplerType.Texture2D, "2darray" => SamplerType.Texture2D | SamplerType.Array,
                "2dms" => SamplerType.Texture2D | SamplerType.Multisample, "2dmsarray" => SamplerType.Texture2D | SamplerType.Multisample | SamplerType.Array,
                "3d" => SamplerType.Texture3D, "cube" => SamplerType.TextureCube, "cubearray" => SamplerType.TextureCube | SamplerType.Array,
                _ => SamplerType.Texture2D,
            };
        }
        public TextureFormat QueryTextureFormat(int handle, int cbufSlot = -1)
        {
            int slot = TexSlot(handle, cbufSlot);
            string f = _job["img"] is JsonObject t && slot >= 0 && t[slot.ToString()] is JsonObject o ? (string)o["fmt"] : null;
            if (f == null) { Notes.Add($"image handle {handle:X} slot {slot} has no known format, taken as R8G8B8A8Unorm"); return TextureFormat.R8G8B8A8Unorm; }
            return Enum.TryParse(f, true, out TextureFormat tf) ? tf : TextureFormat.R8G8B8A8Unorm;
        }
        int Local(int i) => _job["local"] is JsonArray a && a.Count > i ? (int)a[i] : 1;
        public int QueryComputeLocalSizeX() => Local(0);
        public int QueryComputeLocalSizeY() => Local(1);
        public int QueryComputeLocalSizeZ() => Local(2);
        public int QueryComputeSharedMemorySize() => _job["shared"] is JsonNode n ? (int)n : 0;   // sum of the paired PC shader's groupshared declarations
        static AttributeType Attr(string s) => s switch { "sint" => AttributeType.Sint, "uint" => AttributeType.Uint, _ => AttributeType.Float };
        public GpuGraphicsState QueryGraphicsState()
        {
            Array32<AttributeType> attr = default; Array8<AttributeType> fout = default;
            if (_job["attr"] is JsonObject a) foreach (var kv in a) attr[int.Parse(kv.Key)] = Attr((string)kv.Value);
            if (_job["fout"] is JsonObject f) foreach (var kv in f) fout[int.Parse(kv.Key)] = Attr((string)kv.Value);
            bool earlyZ = _job["early_z"] is JsonNode ez && (bool)ez;   // early fragment tests (dmsh_build.py: control section +0x718 bit 0, plan item 239)
            return new GpuGraphicsState(earlyZ, InputTopology.Triangles, false, TessPatchType.Triangles, TessSpacing.EqualSpacing,
                false, false, false, _nearMinusW /* depth mode: GL convention */, false, 1f, AlphaTestOp.Always, 0f, attr, false, fout, false,
                false /* no Y flip */, true /* origin upper-left */, false);
        }
        public bool QueryHostSupportsTextureShadowLod() => true;
        // Metal has no 64-bit floats ("'double' is not supported in Metal"): the translator's own DoubleToFloat pass runs double arithmetic as float and
        // turns PackDouble2x32 / UnpackDouble2x32 into IEEE double <-> float helpers, so register pairs still hold double bits (Tears of the Kingdom's
        // vertex shaders do DMUL / DFMA dot products on uniform data; 7 of them failed to compile)
        public bool QueryHostSupportsShaderFloat64() => false;
        // Metal has no depth clip control: with the GL convention the translator remaps z itself (EmitterContext.PrepareForVertexReturn)
        public bool QueryHostSupportsDepthClipControl() => !_nearMinusW;
        // Job reduced = true: relaxed precision - the translator marks NoContraction only on operations that must be exact (the default marks all: keeps the bit-exact results of Maxwell's separate rounding, at the cost of many optimization-disabled spvFMul etc. in the MSL)
        public bool QueryHostReducedPrecision() => _job["reduced"] is JsonNode n && (bool)n;
        // Job "subgroup" (the Vulkan bundle, dmsh_vk.py; plan item 311): the host's subgroup size. Adreno runs waves of 64 or 128 lanes; above 32 the
        // translator keeps Maxwell's 32-lane warps inside it (lane id & 31, the ballot word of the lane's own 32, shuffles within its 32). At 32 a
        // shuffle's source lane lost the wave's upper lanes (41 of manosaba's modules shuffle by lane; nx2android 44eca7e, MIT). Metal's SIMD groups: 32
        public int QueryHostSubgroupSize() => _job["subgroup"] is JsonNode n ? (int)n : 32;
        // Job "gather_bias" (the Vulkan bundle): bits of the host's gather coordinate precision; the translator moves gather coordinates off the texel
        // edge, where Adreno rounds the other way from Maxwell (nx2android 44eca7e: Tears of the Kingdom's tiled lighting drew dark 16x16 boxes, Eden
        // #4565). Metal: 0, none
        public int QueryHostGatherBiasPrecision() => _job["gather_bias"] is JsonNode n ? (int)n : 0;
    }

    static int Main(string[] args)
    {
        if (args.Length == 1 && args[0] == "--source-sha")   // first 16 digits of the sha256 of the embedded Program.cs + csproj (same convention as dmsh_chain.py)
        {
            var asm = typeof(SwshXlate).Assembly; var ms = new MemoryStream();
            foreach (var n in new[] { "src0", "src1" }) asm.GetManifestResourceStream(n).CopyTo(ms);
            Console.WriteLine(Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(ms.ToArray()))[..16].ToLowerInvariant());
            return 0;
        }
        var jobs = JsonNode.Parse(File.ReadAllText(args[0])).AsArray(); string outDir = args[1]; Directory.CreateDirectory(outDir);
        int ok = 0, bad = 0;
        foreach (JsonObject job in jobs)
        {
            string key = (string)job["key"];
            try
            {
                byte[] code = File.ReadAllBytes((string)job["code"]);
                var acc = new Accessor(code, job);
                // Job debug = false: turn off DebugMode (DebugMode skips the AST optimizer and the output is full of temporaries; semantics unchanged)
                var flags = (job["debug"] is JsonNode dbg && !(bool)dbg ? 0 : TranslationFlags.DebugMode) | ((string)job["stage"] == "cs" ? TranslationFlags.Compute : 0);
                var ctx = Translator.CreateContext(0, acc, new TranslationOptions(TargetLanguage.Spirv, TargetApi.Vulkan, flags));
                ShaderProgram p = ctx.Translate();
                // Second pass: a size query on an image handle was answered with the default, and this shader accessed an image through the same handle (same c[2] word offset) → retranslate with that image's type
                if (acc.ImgQueryDefaulted)
                {
                    var byWord = new Dictionary<int, SamplerType>();
                    foreach (var t in p.Info.Images) if (t.CbufSlot == 2) byWord[t.HandleIndex] = t.Type & (SamplerType.Mask | SamplerType.Array | SamplerType.Multisample);
                    if (byWord.Count != 0)
                    {
                        acc = new Accessor(code, job, byWord);
                        p = Translator.CreateContext(0, acc, new TranslationOptions(TargetLanguage.Spirv, TargetApi.Vulkan, flags)).Translate();
                    }
                }
                File.WriteAllBytes(Path.Combine(outDir, key + ".spv"), VertexCounterLowering.Apply(code, (string)job["stage"], p.BinaryCode, acc.Notes));
                var res = new JsonObject
                {
                    ["key"] = key, ["stage"] = (string)job["stage"], ["notes"] = new JsonArray(acc.Notes.ConvertAll(n => (JsonNode)n).ToArray()),
                    ["cbufs"] = new JsonArray(), ["sbufs"] = new JsonArray(), ["textures"] = new JsonArray(), ["images"] = new JsonArray(),
                    ["uses_frag_coord"] = p.Info.UsesFragCoord, ["frag_out_map"] = p.Info.FragmentOutputMap,
                };
                foreach (var b in p.Info.CBuffers) res["cbufs"].AsArray().Add(new JsonObject { ["binding"] = b.Binding, ["slot"] = b.Slot });
                foreach (var b in p.Info.SBuffers) res["sbufs"].AsArray().Add(new JsonObject { ["binding"] = b.Binding, ["slot"] = b.Slot, ["cb_slot"] = b.SbCbSlot, ["cb_offset"] = b.SbCbOffset, ["flags"] = (int)b.Flags });
                foreach (var (list, name) in new[] { (p.Info.Textures, "textures"), (p.Info.Images, "images") })
                    foreach (var t in list) res[name].AsArray().Add(new JsonObject { ["binding"] = t.Binding, ["type"] = (int)t.Type, ["format"] = t.Format.ToString(), ["cbuf_slot"] = t.CbufSlot, ["handle"] = t.HandleIndex, ["array"] = t.ArrayLength, ["separate"] = t.Separate, ["flags"] = (int)t.Flags });
                File.WriteAllText(Path.Combine(outDir, key + ".res.json"), res.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
                ok++;
            }
            catch (Exception e) { bad++; Console.Error.WriteLine($"[red] {key}: {e.GetType().Name}: {e.Message.Split('\n')[0]}"); if (Environment.GetEnvironmentVariable("DMSH_STACK") != null) Console.Error.WriteLine(e.StackTrace); }
        }
        Console.WriteLine($"translated {ok}, failed {bad}");
        return bad == 0 ? 0 : 1;
    }
}

// Tears of the Kingdom 1.4.3's visibility counter (one vertex shader, matched by the SHA-256 of its code: capture 1eee2726aae7493e): each invocation
// that passes its test joins a subgroup ballot, and the highest active lane adds popcount(ballot) to s1[0]. SPIRV-Cross gives a Metal vertex function no
// subgroup builtins ("Subgroup builtins are not available in this type of function"), so the shader never built and its draws were skipped. Lowered to
// one-lane groups: lane id 0 and ballot(true) = (1, 0, 0, 0), so every passing invocation adds one; the atomic's result is unused, so the final sum
// (mod 2^32) is the same. Not a general subgroup fallback: it runs only on these analysed bytes.
// From MacNativeGames (games/totk/tools/totk_xlate/VertexCounterLowering.cs, MIT) as seedds/nxnative carries it (771f62f, MIT).
static class VertexCounterLowering
{
    const string InstanceCodeHash = "F5F4D23149565ACD852F6B7091F64CC8CB3AA019CCB33BA3F88CED62C6D1FF7B";

    internal static byte[] Apply(byte[] code, string stage, byte[] spirv, List<string> notes)
    {
        if (stage != "vs" || Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(code)) != InstanceCodeHash) return spirv;
        uint[] w = new uint[spirv.Length / 4]; Buffer.BlockCopy(spirv, 0, w, 0, spirv.Length);
        uint lane = 0, uintType = 0, vectorType = 0, trueId = 0; int ballots = 0, loads = 0;
        for (int i = 5; i < w.Length; i += (int)(w[i] >> 16))
        {
            uint op = w[i] & 65535;
            if (op == 71 && w[i + 2] == 11 && w[i + 3] == 41) lane = w[i + 1];   // OpDecorate BuiltIn SubgroupLocalInvocationId
            if (op == 21 && w[i + 2] == 32 && w[i + 3] == 0) uintType = w[i + 1];   // OpTypeInt 32 unsigned
            if (op == 41) trueId = w[i + 2];   // OpConstantTrue
            if (op == 339) { vectorType = w[i + 1]; ballots++; if (w[i + 4] != trueId) throw new InvalidOperationException("visibility counter: ballot predicate changed"); }   // OpGroupNonUniformBallot
        }
        if (lane == 0 || uintType == 0 || vectorType == 0 || ballots != 1) throw new InvalidOperationException("visibility counter: SPIR-V shape changed");
        uint zero = w[3]++, one = w[3]++, mask = w[3]++;   // new ids past the bound
        var result = new List<uint>(w.Length + 16); result.AddRange(w.AsSpan(0, 5).ToArray());
        bool constants = false;
        for (int i = 5; i < w.Length; i += (int)(w[i] >> 16))
        {
            uint op = w[i] & 65535;
            if (op == 54 && !constants)   // before the first OpFunction: uint 0, uint 1, uvec4(1, 0, 0, 0)
            {
                result.AddRange(new uint[] { (4u << 16) | 43, uintType, zero, 0, (4u << 16) | 43, uintType, one, 1, (7u << 16) | 44, vectorType, mask, one, zero, zero, zero });
                constants = true;
            }
            if (op == 61 && w[i + 3] == lane) { result.AddRange(new uint[] { (4u << 16) | 83, w[i + 1], w[i + 2], zero }); loads++; }   // OpLoad lane id -> OpCopyObject 0
            else if (op == 339) result.AddRange(new uint[] { (4u << 16) | 83, w[i + 1], w[i + 2], mask });   // ballot(true) -> OpCopyObject (1, 0, 0, 0)
            else result.AddRange(w.AsSpan(i, (int)(w[i] >> 16)).ToArray());
        }
        if (loads != 1 || !constants) throw new InvalidOperationException("visibility counter: lane id use changed");
        notes.Add("visibility counter lowered to one-lane groups (one atomic add per passing vertex)");
        byte[] output = new byte[result.Count * 4]; Buffer.BlockCopy(result.ToArray(), 0, output, 0, output.Length);
        return output;
    }
}
