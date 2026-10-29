using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

[assembly: System.Reflection.AssemblyVersion("0.0.1.0")]
[assembly: System.Reflection.AssemblyFileVersion("0.0.1.0")]

static class Program
{
    [STAThread]
    static void Main()
    {
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        Application.Run(new MainWindow());
    }
}

public class MainWindow : Form
{
    readonly ListBox files = new ListBox { Dock = DockStyle.Fill, SelectionMode = SelectionMode.MultiExtended, HorizontalScrollbar = true };
    readonly ComboBox format = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 160 };
    readonly TextBox output = new TextBox { Dock = DockStyle.Fill };
    readonly TextBox password = new TextBox { Width = 180, UseSystemPasswordChar = true };
    readonly TextBox log = new TextBox { Dock = DockStyle.Fill, Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical, BackColor = Color.FromArgb(245, 247, 250) };
    readonly Label status = new Label { AutoSize = true, Text = "파일이나 폴더를 추가해 주세요.", Padding = new Padding(0, 6, 0, 0) };
    readonly ProgressBar progress = new ProgressBar { Width = 130, Height = 20, Visible = false };
    readonly TabControl tabs = new TabControl { Dock = DockStyle.Fill };
    readonly CheckBox keep = new CheckBox { Text = "기존 파일 덮어쓰지 않기", Checked = true, AutoSize = true };
    readonly List<Control> editing = new List<Control>();
    readonly Button run = new Button { Text = "압축하기", AutoSize = true, Height = 36 };
    bool busy;
    string engine { get { return Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "dazip.exe"); } }
    bool extracting { get { return tabs.SelectedIndex == 1; } }

    public MainWindow()
    {
        Text = "DaZipHop — 압축 / 압축 해제";
        Font = new Font("맑은 고딕", 10F);
        ClientSize = new Size(820, 700); MinimumSize = new Size(720, 640);
        StartPosition = FormStartPosition.CenterScreen;
        var root = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(22), ColumnCount = 1, RowCount = 9 };
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 44));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 34));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 50));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 55));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 95));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 62));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 46));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 28));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 45));
        Controls.Add(root);
        root.Controls.Add(new Label { Text = "DaZipHop", Font = new Font(Font.FontFamily, 22F, FontStyle.Bold), AutoSize = true }, 0, 0);
        root.Controls.Add(new Label { Text = "파일을 선택하고, 원하는 형식으로 압축하거나 풀어 보세요.", AutoSize = true }, 0, 1);
        tabs.TabPages.Add("파일 압축"); tabs.TabPages.Add("압축 해제");
        root.Controls.Add(tabs, 0, 2); editing.Add(tabs);
        var source = new TableLayoutPanel { Dock = DockStyle.Fill, RowCount = 2, ColumnCount = 1 };
        source.RowStyles.Add(new RowStyle(SizeType.Absolute, 38)); source.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        var buttons = new FlowLayoutPanel { Dock = DockStyle.Fill };
        buttons.Controls.Add(Button("파일 추가", AddFiles));
        buttons.Controls.Add(Button("폴더 추가", AddFolder));
        buttons.Controls.Add(Button("선택 삭제", delegate { foreach (var item in files.SelectedItems.Cast<string>().ToArray()) files.Items.Remove(item); }));
        buttons.Controls.Add(Button("모두 비우기", delegate { files.Items.Clear(); }));
        source.Controls.Add(buttons, 0, 0); source.Controls.Add(files, 0, 1);
        root.Controls.Add(source, 0, 3); editing.Add(files);
        files.AllowDrop = true;
        files.DragEnter += delegate(object s, DragEventArgs e) { if (!busy && e.Data.GetDataPresent(DataFormats.FileDrop)) e.Effect = DragDropEffects.Copy; };
        files.DragDrop += delegate(object s, DragEventArgs e) { if (!busy) AddPaths((string[])e.Data.GetData(DataFormats.FileDrop)); };
        var destination = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 3, RowCount = 2, Padding = new Padding(0, 10, 0, 0) };
        destination.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 92)); destination.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100)); destination.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 100));
        destination.Controls.Add(new Label { Text = "압축 형식", AutoSize = true }, 0, 0);
        format.Items.AddRange(new object[] { "zip", "7z", "tar", "tar.gz", "tar.bz2", "tar.xz", "tar.zst", "tar.lz4", "tar.lzma", "gz (파일 1개)", "bz2 (파일 1개)", "xz (파일 1개)", "zst (파일 1개)", "lz4 (파일 1개)" });
        format.SelectedIndex = 0; destination.Controls.Add(format, 1, 0); editing.Add(format);
        destination.Controls.Add(new Label { Text = "저장 위치", AutoSize = true }, 0, 1);
        destination.Controls.Add(output, 1, 1); editing.Add(output);
        destination.Controls.Add(Button("찾아보기", ChooseOutput), 2, 1);
        root.Controls.Add(destination, 0, 4);
        var options = new FlowLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(0, 8, 0, 0) };
        options.Controls.Add(new Label { Text = "암호 (선택)", AutoSize = true, Padding = new Padding(0, 4, 0, 0) });
        options.Controls.Add(password); options.Controls.Add(keep); editing.Add(password); editing.Add(keep);
        root.Controls.Add(options, 0, 5);
        var actions = new FlowLayoutPanel { Dock = DockStyle.Fill };
        run.Click += async delegate { await Execute(); }; actions.Controls.Add(run); actions.Controls.Add(progress); actions.Controls.Add(status);
        root.Controls.Add(actions, 0, 6);
        root.Controls.Add(new Label { Text = "작업 기록 · 파일 목록에 끌어다 놓아도 추가됩니다.", AutoSize = true }, 0, 7);
        root.Controls.Add(log, 0, 8);
        tabs.SelectedIndexChanged += delegate { files.Items.Clear(); output.Clear(); password.Clear(); UpdateMode(); };
        format.SelectedIndexChanged += delegate { output.Clear(); };
        FormClosing += delegate(object s, FormClosingEventArgs e) { if (busy) { e.Cancel = true; MessageBox.Show(this, "작업이 끝난 후 창을 닫아 주세요."); } };
        UpdateMode();
    }

    Button Button(string text, Action action)
    {
        var b = new Button { Text = text, AutoSize = true };
        b.Click += delegate { action(); }; editing.Add(b); return b;
    }
    void UpdateMode()
    {
        format.Enabled = !extracting; keep.Enabled = extracting;
        run.Text = extracting ? "압축 해제하기" : "압축하기";
        status.Text = extracting ? "압축파일을 추가해 주세요." : "파일이나 폴더를 추가해 주세요.";
    }
    void AddPaths(IEnumerable<string> paths)
    {
        foreach (string p in paths) {
            string full = Path.GetFullPath(p);
            if (extracting && Directory.Exists(full)) { MessageBox.Show(this, "압축 해제할 파일을 선택해 주세요."); continue; }
            if (!files.Items.Cast<string>().Contains(full, StringComparer.OrdinalIgnoreCase)) files.Items.Add(full);
        }
        status.Text = files.Items.Count + "개 선택됨";
    }
    void AddFiles()
    {
        using (var d = new OpenFileDialog { Multiselect = true, Title = extracting ? "압축 해제할 파일 선택" : "압축할 파일 선택", Filter = extracting ? "압축파일|*.zip;*.7z;*.rar;*.rar5;*.tar;*.gz;*.tgz;*.bz2;*.xz;*.zst;*.lz4;*.cab;*.iso;*.cpio;*.xar;*.ar;*.lzma;*.lzh;*.lha;*.rpm;*.deb|모든 파일|*.*" : "모든 파일|*.*" })
            if (d.ShowDialog(this) == DialogResult.OK) AddPaths(d.FileNames);
    }
    void AddFolder()
    {
        if (extracting) { MessageBox.Show(this, "압축 해제 탭에서는 압축파일을 추가해 주세요."); return; }
        using (var d = new FolderBrowserDialog { Description = "압축할 폴더 선택" }) if (d.ShowDialog(this) == DialogResult.OK) AddPaths(new[] { d.SelectedPath });
    }
    string Extension { get { return format.SelectedItem.ToString().Split(' ')[0]; } }
    void ChooseOutput()
    {
        if (extracting) {
            using (var d = new FolderBrowserDialog { Description = "압축을 풀 폴더 선택 (여러 압축파일은 각각 하위 폴더에 해제)" }) if (d.ShowDialog(this) == DialogResult.OK) output.Text = d.SelectedPath;
        } else {
            string ext = Extension;
            using (var d = new SaveFileDialog { Title = "압축파일 저장", Filter = ext + " 압축파일|*." + ext, DefaultExt = ext, AddExtension = true, FileName = "압축파일." + ext })
                if (d.ShowDialog(this) == DialogResult.OK) output.Text = d.FileName;
        }
    }
    public static string Quote(string value)
    {
        // Windows CommandLineToArgvW 규칙: 따옴표 앞과 문자열 끝의 역슬래시 처리.
        var b = new StringBuilder("\""); int slashes = 0;
        foreach (char c in value) {
            if (c == '\\') { slashes++; continue; }
            if (c == '"') b.Append('\\', slashes * 2 + 1); else b.Append('\\', slashes);
            b.Append(c); slashes = 0;
        }
        b.Append('\\', slashes * 2); return b.Append('"').ToString();
    }
    public static string CommonParent(string[] paths)
    {
        string parent = Path.GetDirectoryName(paths[0]);
        while (parent != null && paths.Any(p => !p.StartsWith(parent.TrimEnd('\\') + "\\", StringComparison.OrdinalIgnoreCase))) parent = Path.GetDirectoryName(parent);
        if (parent == null) throw new InvalidOperationException("같은 드라이브의 파일을 선택해 주세요.");
        return parent;
    }
    public static Tuple<int, string> RunEngine(string executable, string cwd, List<string> args)
    {
        var info = new ProcessStartInfo(executable, string.Join(" ", args.Select(Quote))) {
            WorkingDirectory = cwd, UseShellExecute = false, CreateNoWindow = true,
            RedirectStandardOutput = true, RedirectStandardError = true, RedirectStandardInput = true,
            StandardOutputEncoding = Encoding.UTF8, StandardErrorEncoding = Encoding.UTF8
        };
        using (var p = Process.Start(info)) {
            p.StandardInput.Close();
            Task<string> stdout = p.StandardOutput.ReadToEndAsync(), stderr = p.StandardError.ReadToEndAsync();
            p.WaitForExit(); Task.WaitAll(stdout, stderr);
            return Tuple.Create(p.ExitCode, stdout.Result + stderr.Result);
        }
    }
    async Task Execute()
    {
        if (busy) return;
        try {
            if (!File.Exists(engine)) throw new InvalidOperationException("같은 폴더에 dazip.exe가 필요합니다.");
            string[] inputs = files.Items.Cast<string>().ToArray();
            if (inputs.Length == 0) throw new InvalidOperationException("먼저 파일 또는 폴더를 추가해 주세요.");
            if (inputs.Any(p => !File.Exists(p) && !Directory.Exists(p))) throw new InvalidOperationException("선택한 파일 또는 폴더가 존재하지 않습니다.");
            bool extract = extracting; string ext = Extension, secret = password.Text;
            if (!extract && format.SelectedIndex >= 9 && (inputs.Length != 1 || !File.Exists(inputs[0]))) throw new InvalidOperationException("이 형식은 파일 하나만 압축할 수 있습니다. 여러 파일이나 폴더는 ZIP 또는 TAR 형식을 선택해 주세요.");
            if (!extract && secret.Length > 0 && ext != "zip") throw new InvalidOperationException("암호를 사용하려면 ZIP 형식을 선택해 주세요.");
            if (string.IsNullOrWhiteSpace(output.Text)) { ChooseOutput(); if (string.IsNullOrWhiteSpace(output.Text)) return; }
            string target = Path.GetFullPath(output.Text);
            if (!extract && !target.EndsWith("." + ext, StringComparison.OrdinalIgnoreCase)) { target += "." + ext; output.Text = target; }
            if (!extract && inputs.Any(p => string.Equals(p, target, StringComparison.OrdinalIgnoreCase))) throw new InvalidOperationException("원본 파일과 저장 파일의 경로가 같습니다.");
            if (!extract && File.Exists(target) && MessageBox.Show(this, "기존 압축파일을 덮어쓸까요?\n" + target, "덮어쓰기", MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes) return;
            if (extract && File.Exists(target)) throw new InvalidOperationException("해제 위치는 폴더로 지정해 주세요.");
            string cwd = extract ? AppDomain.CurrentDomain.BaseDirectory : CommonParent(inputs);
            var jobs = new List<List<string>>();
            if (extract) {
                var used = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                foreach (string input in inputs) {
                    string dest = target;
                    if (inputs.Length > 1) {
                        string name = ArchiveName(input); dest = Path.Combine(target, name); int n = 2;
                        while (!used.Add(dest)) dest = Path.Combine(target, name + " (" + n++ + ")");
                    }
                    var args = new List<string> { "x", "-v", "-C", dest };
                    if (keep.Checked) args.Add("-k");
                    if (secret.Length > 0) args.AddRange(new[] { "-p", secret });
                    args.Add("--"); args.Add(input); jobs.Add(args);
                }
            } else {
                Directory.CreateDirectory(Path.GetDirectoryName(target));
                var args = new List<string> { "c", "-v", "-f", ext };
                if (secret.Length > 0) args.AddRange(new[] { "-p", secret });
                args.Add("--"); args.Add(target);
                args.AddRange(inputs.Select(p => p.Substring(cwd.TrimEnd('\\').Length + 1))); jobs.Add(args);
            }
            busy = true; foreach (var c in editing) c.Enabled = false; run.Enabled = false;
            progress.Visible = true; progress.Style = ProgressBarStyle.Marquee;
            status.Text = "작업 중…"; log.Clear(); int failed = 0;
            foreach (var args in jobs) {
                var result = await Task.Run(() => RunEngine(engine, cwd, args));
                log.AppendText(result.Item2 + Environment.NewLine); if (result.Item1 != 0) failed++;
            }
            status.Text = failed == 0 ? "완료되었습니다." : "실패: " + failed + "건 · 기록을 확인하세요.";
            if (failed != 0) MessageBox.Show(this, "작업 중 오류가 발생했습니다. 작업 기록을 확인해 주세요.\n암호가 필요한 파일은 암호를 입력한 후 다시 실행하세요.", "작업 오류", MessageBoxButtons.OK, MessageBoxIcon.Warning);
        } catch (Exception ex) { MessageBox.Show(this, ex.Message, "확인", MessageBoxButtons.OK, MessageBoxIcon.Warning); }
        finally {
            busy = false; foreach (var c in editing) c.Enabled = true; run.Enabled = true;
            progress.Visible = false; format.Enabled = !extracting; keep.Enabled = extracting;
        }
    }
    static string ArchiveName(string path)
    {
        string name = Path.GetFileName(path);
        foreach (string ext in new[] { ".tar.gz", ".tar.bz2", ".tar.xz", ".tar.zst", ".tar.lz4", ".tar.lzma" })
            if (name.EndsWith(ext, StringComparison.OrdinalIgnoreCase)) return name.Substring(0, name.Length - ext.Length);
        string result = Path.GetFileNameWithoutExtension(name); return string.IsNullOrWhiteSpace(result) ? "압축해제" : result;
    }
}
