using System;
using System.IO;
using System.Diagnostics;
using System.Windows.Forms;
using System.ComponentModel;
using System.Security.Principal;
using Microsoft.Win32;

static class Setup
{
    const string Key = @"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\DaZipHop";
    internal static readonly string Folder = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "DaZipHop");
    static string Shortcut { get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonPrograms), "DaZipHop.lnk"); } }
    static bool Admin { get { using (var identity = WindowsIdentity.GetCurrent()) return new WindowsPrincipal(identity).IsInRole(WindowsBuiltInRole.Administrator); } }

    internal static bool Handle(string[] args)
    {
        if (args.Length == 0 || (args[0] != "--install" && args[0] != "--uninstall" && args[0] != "--remove-worker")) return false;
        bool remove = args[0] != "--install";
        if (args[0] != "--remove-worker") { Launch(remove); return true; }
        try {
            if (!Admin) throw new InvalidOperationException("관리자 권한이 필요합니다.");
            if (args.Length != 2) throw new InvalidOperationException("잘못된 제거 요청입니다.");
            int parent;
            if (!int.TryParse(args[1], out parent)) throw new InvalidOperationException("잘못된 프로세스 번호입니다.");
            try { using (var p = Process.GetProcessById(parent)) { if (!p.WaitForExit(30000)) throw new IOException("실행 중인 DaZipHop을 닫고 다시 시도해 주세요."); } }
            catch (ArgumentException) { }
            Remove();
            MessageBox.Show("DaZipHop 제거가 완료되었습니다.", "DaZipHop");
        } catch (Exception ex) { MessageBox.Show(ex.Message, "제거 실패", MessageBoxButtons.OK, MessageBoxIcon.Error); }
        return true;
    }

    internal static bool Launch(bool remove)
    {
        if (MessageBox.Show(remove ? "모든 사용자의 DaZipHop을 제거할까요?" : "모든 사용자를 위해 DaZipHop을 설치할까요?\n" + Folder,
            "DaZipHop", MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes) return false;
        try {
            string executable = Application.ExecutablePath;
            string arguments = "--install-worker";
            if (remove) {
                // 실행 중인 설치본을 지울 수 있도록 독립 복사본에서 제거합니다.
                executable = Path.Combine(Path.GetTempPath(), "DaZipHop-remove-" + Guid.NewGuid().ToString("N") + ".exe");
                File.Copy(Application.ExecutablePath, executable);
                arguments = "--remove-worker " + Process.GetCurrentProcess().Id;
            }
            using (var p = Process.Start(new ProcessStartInfo(executable, arguments) { UseShellExecute = true, Verb = "runas" })) {
                if (!remove) { p.WaitForExit(); if (p.ExitCode != 0) throw new IOException("설치가 완료되지 않았습니다."); }
            }
            return remove;
        } catch (Win32Exception ex) {
            MessageBox.Show(ex.NativeErrorCode == 1223 ? "관리자 권한 요청을 취소했습니다." : ex.Message, "DaZipHop");
        } catch (Exception ex) { MessageBox.Show(ex.Message, "DaZipHop", MessageBoxButtons.OK, MessageBoxIcon.Error); }
        return false;
    }

    internal static int Install()
    {
        try {
            if (!Admin) throw new InvalidOperationException("관리자 권한이 필요합니다.");
            string source = AppDomain.CurrentDomain.BaseDirectory;
            if (!File.Exists(Path.Combine(source, "dazip.exe"))) throw new FileNotFoundException("같은 폴더에 dazip.exe가 필요합니다.");
            Directory.CreateDirectory(Folder);
            foreach (string name in new[] { "DaZipHop.exe", "dazip.exe" }) {
                string dest = Path.Combine(Folder, name);
                if (!string.Equals(Path.GetFullPath(Path.Combine(source, name)), dest, StringComparison.OrdinalIgnoreCase)) File.Copy(Path.Combine(source, name), dest, true);
            }
            string target = Path.Combine(Folder, "DaZipHop.exe");
            Type shellType = Type.GetTypeFromProgID("WScript.Shell");
            object shell = Activator.CreateInstance(shellType);
            object link = shellType.InvokeMember("CreateShortcut", System.Reflection.BindingFlags.InvokeMethod, null, shell, new object[] { Shortcut });
            try {
                Type type = link.GetType();
                type.InvokeMember("TargetPath", System.Reflection.BindingFlags.SetProperty, null, link, new object[] { target });
                type.InvokeMember("WorkingDirectory", System.Reflection.BindingFlags.SetProperty, null, link, new object[] { Folder });
                type.InvokeMember("Save", System.Reflection.BindingFlags.InvokeMethod, null, link, null);
            } finally { System.Runtime.InteropServices.Marshal.FinalReleaseComObject(link); System.Runtime.InteropServices.Marshal.FinalReleaseComObject(shell); }
            using (var key = Registry.LocalMachine.CreateSubKey(Key)) {
                key.SetValue("DisplayName", "DaZipHop"); key.SetValue("DisplayVersion", "0.0.2");
                key.SetValue("InstallLocation", Folder); key.SetValue("DisplayIcon", target);
                key.SetValue("UninstallString", MainWindow.Quote(target) + " --uninstall");
                key.SetValue("NoModify", 1); key.SetValue("NoRepair", 1);
            }
            MessageBox.Show("설치가 완료되었습니다. 시작 메뉴에서 DaZipHop을 실행할 수 있습니다.", "DaZipHop");
            return 0;
        } catch (Exception ex) { MessageBox.Show(ex.Message, "설치 실패", MessageBoxButtons.OK, MessageBoxIcon.Error); return 1; }
    }

    static void Remove()
    {
        // 사용자 파일이나 알 수 없는 파일은 재귀 삭제하지 않습니다.
        foreach (string name in new[] { "DaZipHop.exe", "dazip.exe" }) File.Delete(Path.Combine(Folder, name));
        File.Delete(Shortcut);
        Registry.LocalMachine.DeleteSubKeyTree(Key, false);
        if (Directory.Exists(Folder) && Directory.GetFileSystemEntries(Folder).Length == 0) Directory.Delete(Folder);
        // 임시 실행 파일은 Windows가 재시작할 때 정리합니다.
        MoveFileEx(Application.ExecutablePath, null, 4);
    }
    [System.Runtime.InteropServices.DllImport("kernel32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode, SetLastError = true)]
    static extern bool MoveFileEx(string existing, string destination, int flags);
}
