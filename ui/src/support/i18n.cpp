#include "support/i18n.hpp"

#include <iterator>
#include <unordered_map>

namespace lnb::i18n {

namespace {

// One translation entry: the English source text (the lookup key) and its
// renderings. Entries are grouped by the window or page they belong to; within a
// group they follow the source order, which keeps the table easy to diff against
// the code. Every language occupies its own column so adding one does not
// disturb the others; an entry always carries all of them.
struct Entry {
  std::string_view en;
  const char* zh_hant; // Traditional Chinese, UTF-8
  const char* zh_hans; // Simplified Chinese, UTF-8
  const char* ja;      // Japanese, UTF-8
  const char* de;      // German, UTF-8
};

constexpr Entry kTranslations[] = {
    // General.
    {"Close", "關閉", "关闭", "閉じる", "Schließen"},
    {"Save", "儲存", "保存", "保存", "Speichern"},
    {"Cancel", "取消", "取消", "キャンセル", "Abbrechen"},
    {"Create", "建立", "创建", "作成", "Erstellen"},
    {"Edit", "編輯", "编辑", "編集", "Bearbeiten"},
    {"Copy", "複製", "复制", "コピー", "Kopieren"},
    {"Select all", "全選", "全选", "すべて選択", "Alles auswählen"},
    {"Open", "開啟", "打开", "開く", "Öffnen"},
    {"Run", "執行", "运行", "実行", "Ausführen"},
    {"Settings", "設定", "设置", "設定", "Einstellungen"},
    {"Configuration", "配置", "配置", "設定", "Konfiguration"},
    {"Metrics", "度量", "度量", "メトリクス", "Metriken"},
    {"Assistant", "助手", "助手", "アシスタント", "Assistent"},
    {"Account", "帳戶", "帐户", "アカウント", "Konto"},
    {"About", "關於", "关于", "このアプリについて", "Über"},
    {"Language", "語言", "语言", "言語", "Sprache"},
    {"Language changed", "語言已更改", "语言已更改", "言語を変更しました", "Sprache geändert"},
    {"The interface language will change after Local Notebook restarts. Restart now?", "介面語言將在 Local Notebook 重啟後更改。現在重啟嗎？", "界面语言将在 Local Notebook 重启后更改。现在重启吗？", "インターフェースの言語は Local Notebook の再起動後に変更されます。今すぐ再起動しますか？", "Die Sprache der Oberfläche ändert sich nach einem Neustart von Local Notebook. Jetzt neu starten?"},
    // Error messages shown in the transcript.
    {"The request failed for an unknown reason.", "請求因未知原因失敗。", "请求因未知原因失败。", "不明な理由でリクエストが失敗しました。", "Die Anfrage ist aus unbekanntem Grund fehlgeschlagen."},
    // Configuration page.
    {"Appearance", "外觀", "外观", "外観", "Darstellung"},
    {"Theme", "主題", "主题", "テーマ", "Design"},
    {"Interface font", "介面字型", "界面字体", "インターフェースフォント", "Schriftart der Oberfläche"},
    {"Interface size", "介面字號", "界面字号", "インターフェースのサイズ", "Größe der Oberfläche"},
    {"Chat font", "聊天字型", "聊天字体", "チャットフォント", "Chat-Schriftart"},
    {"Chat size", "聊天字號", "聊天字号", "チャットのサイズ", "Chat-Größe"},
    {"Monospace font", "等寬字型", "等宽字体", "等幅フォント", "Monospace-Schriftart"},
    {"Monospace size", "等寬字號", "等宽字号", "等幅フォントのサイズ", "Monospace-Größe"},
    {"Mono size", "等寬比例", "等宽比例", "等幅比率", "Mono-Größe"},
    {"Mono size as a fraction of the surrounding text.", "等寬文字字號相對於周圍文字的比例，作用於內聯程式碼與程式碼塊。", "等宽文本字号相对于周围文本的比例，作用于内联代码与代码块。", "等幅テキストのサイズを周囲のテキストに対する比率で指定します。インラインコードとコードブロックの両方に適用されます。", "Mono-Größe als Anteil des umgebenden Textes."},
    {"Tool strip size", "工具條字號", "工具条字号", "ツール行のサイズ", "Größe der Werkzeugleiste"},
    {"Inline summary", "內聯摘要", "内联摘要", "インライン要約", "Inline-Zusammenfassung"},
    {"Compacted into #%1", "已壓縮至 #%1", "已压缩至 #%1", "#%1 に圧縮済み", "In #%1 zusammengefasst"},
    {"Steered", "已插話", "已插话", "割り込み済み", "Gesteuert"},
    {"Steer the running turn", "插入當前回覆", "插入当前回复", "進行中の応答に割り込む", "Den laufenden Durchgang steuern"},
    {"Tool call and result size as a fraction of the message text.", "工具呼叫與結果字號相對於訊息文字的比例。", "工具调用与结果字号相对于消息文本的比例。", "ツール呼び出しと結果のサイズをメッセージ本文に対する比率で指定します。", "Größe von Tool-Aufruf und Ergebnis als Anteil des Nachrichtentextes."},
    {"Composer", "輸入框", "输入框", "入力欄", "Eingabebereich"},
    {"Send with Ctrl+Enter", "使用 Ctrl+Enter 傳送", "使用 Ctrl+Enter 发送", "Ctrl+Enter で送信", "Mit Strg+Enter senden"},
    {"When on, Ctrl+Enter sends and Enter inserts a new line.", "開啟時，Ctrl+Enter 傳送，Enter 換行。", "打开时，Ctrl+Enter 发送，Enter 换行。", "オンのとき、Ctrl+Enter で送信し、Enter で改行します。", "Wenn aktiviert, sendet Strg+Enter und Enter fügt eine neue Zeile ein."},
    {"Transcript", "對話記錄", "对话记录", "トランスクリプト", "Verlauf"},
    {"Rendered messages", "渲染的訊息數", "渲染的消息数", "表示するメッセージ数", "Gerenderte Nachrichten"},
    {"History page size", "歷史分頁大小", "历史分页大小", "履歴のページサイズ", "Seitengröße des Verlaufs"},
    {"Startup", "啟動", "启动", "起動", "Start"},
    {"Open the current assistant automatically on next launch", "下次啟動時自動開啟當前助手", "下次启动时自动打开当前助手", "次回起動時に現在のアシスタントを自動で開く", "Den aktuellen Assistenten beim nächsten Start automatisch öffnen"},
    {"When off, the assistant picker is shown at every launch.", "關閉時，每次啟動都會顯示助手選擇器。", "关闭时，每次启动都会显示助手选择器。", "オフのとき、起動のたびにアシスタント選択画面を表示します。", "Wenn deaktiviert, wird bei jedem Start die Assistentenauswahl angezeigt."},
    {"Background", "背景", "背景", "背景", "Hintergrund"},
    {"Image", "圖片", "图片", "画像", "Bild"},
    {"Path to a background image (optional)", "背景圖片路徑（可選）", "背景图片路径（可选）", "背景画像のパス（任意）", "Pfad zu einem Hintergrundbild (optional)"},
    {"Tint the image", "為圖片著色", "为图片着色", "画像に着色する", "Bild einfärben"},
    {"Restore to defaults", "恢復預設值", "恢复默认值", "デフォルトに戻す", "Auf Standard zurücksetzen"},
    {"Reset this page's fields to their shipped defaults. Nothing is saved until you press Save.", "將此頁面的欄位重置為出廠預設值。在你按下“儲存”之前不會儲存任何內容。", "将此页面的字段重置为出厂默认值。在你按下“保存”之前不会保存任何内容。", "このページの項目を出荷時のデフォルトに戻します。「保存」を押すまで何も保存されません。", "Setzt die Felder dieser Seite auf die werkseitigen Standardwerte zurück. Erst beim Klicken auf Speichern wird etwas gespeichert."},
    {"Application-wide options stored in data/settings.json.", "儲存在 data/settings.json 中的應用級選項。", "保存在 data/settings.json 中的应用级选项。", "data/settings.json に保存されるアプリ全体のオプション。", "Appweite Optionen, gespeichert in data/settings.json."},
    // Metrics page.
    {"Layout", "佈局", "布局", "レイアウト", "Layout"},
    {"Sidebar width", "側邊欄寬度", "侧边栏宽度", "サイドバー幅", "Breite der Seitenleiste"},
    {"Task list width", "任務列表寬度", "任务列表宽度", "タスクリストの幅", "Breite der Aufgabenliste"},
    {"Page margin", "頁面邊距", "页面边距", "ページ余白", "Seitenrand"},
    {"Message spacing", "訊息間距", "消息间距", "メッセージ間隔", "Abstand zwischen Nachrichten"},
    {"Composer height", "輸入框高度", "输入框高度", "入力欄の高さ", "Höhe des Eingabebereichs"},
    {"Expanded height", "展開高度", "展开高度", "展開時の高さ", "Höhe im erweiterten Zustand"},
    {"Session item height", "會話項高度", "会话项高度", "セッション項目の高さ", "Höhe der Session-Einträge"},
    {"Title font size", "標題字號", "标题字号", "タイトル文字サイズ", "Schriftgröße der Titel"},
    {"Flow max width", "正文最大寬度", "正文最大宽度", "本文の最大幅", "Maximale Breite des Verlaufs"},
    {"Bubble radius", "氣泡圓角", "气泡圆角", "吹き出しの角丸", "Radius der Sprechblasen"},
    {"Reading rhythm", "閱讀節奏", "阅读节奏", "リーディングリズム", "Leserhythmus"},
    {"Line heights are percentages (100 is single spacing).", "行高為百分比（100 為單倍行距）。", "行高为百分比（100 为单倍行距）。", "行の高さはパーセントです（100 が単一倍）。", "Zeilenhöhen sind Prozentwerte (100 entspricht einfachem Zeilenabstand)."},
    {"Assistant line height", "助手行高", "助手行高", "アシスタントの行の高さ", "Zeilenhöhe des Assistenten"},
    {"User line height", "使用者行高", "用户行高", "ユーザーの行の高さ", "Zeilenhöhe des Benutzers"},
    {"Paragraph spacing", "段間距", "段间距", "段落間隔", "Absatzabstand"},
    {"List spacing", "列表間距", "列表间距", "リスト間隔", "Listenabstand"},
    {"Reset these metrics to the current theme's own defaults. Nothing is saved until you press Save.", "將這些度量重置為當前主題自身的預設值。在你按下“儲存”之前不會儲存任何內容。", "将这些度量重置为当前主题自身的默认值。在你按下“保存”之前不会保存任何内容。", "これらのメトリクスを現在のテーマ自身のデフォルトに戻します。「保存」を押すまで何も保存されません。", "Setzt diese Metriken auf die Standardwerte des aktuellen Designs zurück. Erst beim Klicken auf Speichern wird etwas gespeichert."},
    {"Sizing and spacing of the interface.", "介面的尺寸與間距。", "界面的尺寸与间距。", "インターフェースのサイズと間隔。", "Größen und Abstände der Oberfläche."},
    // Assistant page.
    {"Assistant memory", "助手記憶", "助手记忆", "アシスタントメモリ", "Gedächtnis des Assistenten"},
    {"Memory updated.", "記憶已更新。", "记忆已更新。", "メモリを更新しました。", "Gedächtnis aktualisiert."},
    {"Recent messages pool", "最近訊息池", "最近消息池", "最近のメッセージプール", "Pool aktueller Nachrichten"},
    {"Update memory now", "立即更新記憶", "立即更新记忆", "今すぐメモリを更新", "Gedächtnis jetzt aktualisieren"},
    {"Updating memory...", "正在更新記憶...", "正在更新记忆...", "メモリを更新中...", "Gedächtnis wird aktualisiert..."},
    {"Memory Update Failed", "記憶更新失敗", "记忆更新失败", "メモリの更新に失敗しました", "Aktualisierung des Gedächtnisses fehlgeschlagen"},
    {"There is no new conversation to merge into memory yet.", "還沒有可合併到記憶中的新對話。", "还没有可合并到记忆中的新对话。", "メモリに統合する新しい会話はまだありません。", "Es gibt noch keine neue Unterhaltung, die in das Gedächtnis übernommen werden kann."},
    {"Update summarization now", "立即更新摘要", "立即更新摘要", "今すぐ要約を更新", "Zusammenfassung jetzt aktualisieren"},
    {"Merge the recent messages of every session into the memory below, ignoring the automatic memory trigger. Idempotent: pressing it again with no new messages changes nothing.", "忽略自動記憶觸發閾值，將所有會話的最近訊息合併到下面的記憶中。冪等：沒有新訊息時再次按下不會改變任何內容。", "忽略自动记忆触发阈值，将所有会话的最近消息合并到下面的记忆中。幂等：没有新消息时再次按下不会改变任何内容。", "自動メモリトリガーを無視して、すべてのセッションの最近のメッセージを下のメモリに統合します。冪等：新しいメッセージがない状態で再度押しても何も変わりません。", "Führt die aktuellen Nachrichten aller Sessions in das unten stehende Gedächtnis zusammen und ignoriert dabei den automatischen Auslöser. Idempotent: Erneutes Klicken ohne neue Nachrichten ändert nichts."},
    {"Automatically maintained record shared across every session. Edit to correct it, or clear it to rebuild it from the transcript.", "在所有會話間共享的自動維護記錄。可以編輯以修正，或清空以從對話記錄重新構建。", "在所有会话间共享的自动维护记录。可以编辑以修正，或清空以从对话记录重新构建。", "すべてのセッションで共有される自動維持の記録です。編集して修正するか、消去してトランスクリプトから再構築してください。", "Automatisch gepflegter Eintrag, der von allen Sessions geteilt wird. Bearbeiten Sie ihn zur Korrektur oder leeren Sie ihn, um ihn aus dem Verlauf neu aufzubauen."},
    {"<b>How assistant memory works</b><br>This memory is shared across every session of the assistant, and it is only rewritten when you ask. <b>Update memory now</b> merges it with each session's newest summary (bounded by <i>Session pool</i>) and with the newest messages across all sessions (bounded by <i>Recent messages pool</i>). The result is capped by <i>Memory budget</i>. Older conversation may therefore be forgotten. Running it again with no new conversation changes nothing. Custom instructions are kept separate and are never touched.", "<b>助手記憶的工作原理</b><br>此記憶在該助手的每個會話之間共享，且只在你要求時才會重寫。<b>立即更新記憶</b> 會將它和每個會話最新的摘要（受 <i>會話池</i> 限制）以及所有會話中最新的訊息（受 <i>最近訊息池</i> 限制）合併。合併結果受 <i>記憶預算</i> 限制。因此較早的對話可能會被遺忘。沒有新對話時再次執行不會改變任何內容。自定義指令單獨儲存，永遠不會被觸碰。", "<b>助手记忆的工作原理</b><br>此记忆在该助手的每个会话之间共享，且只在你要求时才会重写。<b>立即更新记忆</b> 会将它和每个会话最新的摘要（受 <i>会话池</i> 限制）以及所有会话中最新的消息（受 <i>最近消息池</i> 限制）合并。合并结果受 <i>记忆预算</i> 限制。因此较早的对话可能会被遗忘。没有新对话时再次运行不会改变任何内容。自定义指令单独保存，永远不会被触碰。", "<b>アシスタントメモリの仕組み</b><br>このメモリはアシスタントのすべてのセッションで共有され、あなたが求めるときだけ書き換えられます。<b>今すぐメモリを更新</b> は、各セッションの最新の要約（<i>セッションプール</i>で上限）と、すべてのセッションの最新メッセージ（<i>最近のメッセージプール</i>で上限）を統合します。結果は<i>メモリバジェット</i>で上限が設定されます。そのため古い会話は忘れられる可能性があります。新しい会話がない状態で再度実行しても何も変わりません。カスタム指示は別に保持され、決して変更されません。", "<b>So funktioniert das Gedächtnis des Assistenten</b><br>Dieses Gedächtnis wird von allen Sessions des Assistenten geteilt und nur auf Ihre Anforderung hin neu geschrieben. <b>Gedächtnis jetzt aktualisieren</b> führt es mit der neuesten Zusammenfassung jeder Session (begrenzt durch <i>Session-Pool</i>) und mit den neuesten Nachrichten aller Sessions (begrenzt durch <i>Pool aktueller Nachrichten</i>) zusammen. Das Ergebnis wird durch das <i>Gedächtnis-Budget</i> begrenzt. Ältere Unterhaltungen können daher in Vergessenheit geraten. Erneutes Ausführen ohne neue Unterhaltung ändert nichts. Benutzerdefinierte Anweisungen bleiben getrennt und werden nie verändert."},
    {"Updating summarization...", "正在更新摘要...", "正在更新摘要...", "要約を更新中...", "Zusammenfassung wird aktualisiert..."},
    {"Summarization Update Failed", "摘要更新失敗", "摘要更新失败", "要約の更新に失敗しました", "Aktualisierung der Zusammenfassung fehlgeschlagen"},
    {"Nothing to Update", "無可更新內容", "无可更新内容", "更新するものがありません", "Nichts zu aktualisieren"},
    {"There is no new conversation to summarize yet.", "還沒有可摘要的新對話。", "还没有可摘要的新对话。", "要約する新しい会話はまだありません。", "Es gibt noch keine neue Unterhaltung zum Zusammenfassen."},
    {"The assistant open in this window. It is fixed for this run, but its configuration and memory can be edited here.", "此視窗中開啟的助手。它在本次執行期間固定不變，但其配置和記憶可以在此編輯。", "此窗口中打开的助手。它在本次运行期间固定不变，但其配置和记忆可以在此编辑。", "このウィンドウで開いているアシスタント。この実行中は固定ですが、構成とメモリはここで編集できます。", "Der in diesem Fenster geöffnete Assistent. Er ist für diesen Lauf festgelegt, seine Konfiguration und sein Gedächtnis können jedoch hier bearbeitet werden."},
    // Account page.
    {"Current session", "當前會話", "当前会话", "現在のセッション", "Aktuelle Session"},
    {"Startup", "啟動", "启动", "起動", "Start"},
    {"Open at startup", "啟動時開啟", "启动时打开", "起動時に開く", "Beim Start öffnen"},
    {"Open this assistant automatically at startup. When off, the assistant picker is shown every time.", "啟動時自動開啟此助手。關閉時，每次都會顯示助手選擇器。", "启动时自动打开此助手。关闭时，每次都会显示助手选择器。", "起動時にこのアシスタントを自動的に開きます。オフのときは毎回アシスタント選択画面が表示されます。", "Diesen Assistenten beim Start automatisch öffnen. Wenn deaktiviert, wird jedes Mal die Assistentenauswahl angezeigt."},
    {"No session selected", "未選擇會話", "未选择会话", "セッションが選択されていません", "Keine Session ausgewählt"},
    {"Session #%1", "會話 #%1", "会话 #%1", "セッション #%1", "Session #%1"},
    {"The session you are in, and how the app starts.", "你所在的會話，以及應用的啟動方式。", "你所在的会话，以及应用的启动方式。", "現在のセッションと、アプリの起動方法。", "Die Session, in der Sie sich befinden, und wie die App startet."},
    // About page.
    {"A local, assistant-backed notebook.", "一個本地、由助手驅動的筆記本。", "一个本地、由助手驱动的笔记本。", "ローカルでアシスタントを備えたノートブック。", "Ein lokales, assistentengestütztes Notizbuch."},
    {"Version %1", "版本 %1", "版本 %1", "バージョン %1", "Version %1"},
    {"Built with Qt %1", "使用 Qt %1 構建", "使用 Qt %1 构建", "Qt %1 でビルド", "Erstellt mit Qt %1"},
    {"License", "許可", "许可", "ライセンス", "Lizenz"},
    {"Distributed under the terms in LICENSE.txt.", "依據 LICENSE.txt 中的條款分發。", "依据 LICENSE.txt 中的条款分发。", "LICENSE.txt の条項に基づいて配布されています。", "Vertrieben unter den Bedingungen in LICENSE.txt."},
    {"Program information.", "程式資訊。", "程序信息。", "プログラム情報。", "Programminformationen."},
    // Main window.
    {"Toggle sidebar", "切換側邊欄", "切换侧边栏", "サイドバーの表示切替", "Seitenleiste umschalten"},
    {"New Session", "新建會話", "新建会话", "新しいセッション", "Neue Session"},
    {"PINNED", "已固定", "已固定", "ピン留め", "ANGEPINNT"},
    {"SESSIONS", "會話", "会话", "セッション", "SESSIONS"},
    {"Select a session, or create one to start.", "選擇一個會話，或新建一個以開始。", "选择一个会话，或新建一个以开始。", "セッションを選択するか、新規作成して開始してください。", "Wählen Sie eine Session aus oder erstellen Sie eine, um zu beginnen."},
    {"No tasks running", "沒有正在執行的任務", "没有正在运行的任务", "実行中のタスクはありません", "Keine Aufgaben werden ausgeführt"},
    {"1 task running", "1 個任務正在執行", "1 个任务正在运行", "1 件のタスクを実行中", "1 Aufgabe wird ausgeführt"},
    {"%1 tasks running", "%1 個任務正在執行", "%1 个任务正在运行", "%1 件のタスクを実行中", "%1 Aufgaben werden ausgeführt"},
    {"Running", "執行中", "运行中", "実行中", "Wird ausgeführt"},
    {"Completed", "已完成", "已完成", "完了", "Abgeschlossen"},
    {"Failed", "失敗", "失败", "失敗", "Fehlgeschlagen"},
    {"Cancelled", "已取消", "已取消", "キャンセル済み", "Abgebrochen"},
    {"Delete Session", "刪除會話", "删除会话", "セッションの削除", "Session löschen"},
    {"Are you sure you want to delete this session? This cannot be undone.", "確定要刪除此會話嗎？此操作無法撤銷。", "确定要删除此会话吗？此操作无法撤销。", "このセッションを削除してもよろしいですか？この操作は取り消せません。", "Sind Sie sicher, dass Sie diese Session löschen möchten? Dies kann nicht rückgängig gemacht werden."},
    {"summarization trigger: ", "摘要觸發閾值：", "摘要触发阈值：", "要約トリガー: ", "Auslöser für Zusammenfassung: "},
    {"%1 / %2 conversation tokens (system prompt excluded; the summarization update fires at the limit)", "%1 / %2 會話 token（不含系統提示詞；達到上限時執行摘要更新）", "%1 / %2 会话 token（不含系统提示词；达到上限时运行摘要更新）", "%1 / %2 会話トークン（システムプロンプトを除く。上限に達すると要約の更新が実行されます）", "%1 / %2 Konversations-Token (System-Prompt ausgenommen; die Zusammenfassung wird beim Erreichen des Limits ausgelöst)"},
    // Conversation view.
    {"%1 tok", "%1 詞元", "%1 词元", "%1 トークン", "%1 tok"},
    {"%1k tok", "%1k 詞元", "%1k 词元", "%1k トークン", "%1k tok"},
    {"Type a message...  (Ctrl+Enter to send, Enter for a new line)", "輸入訊息...  （Ctrl+Enter 傳送，Enter 換行）", "输入消息...  （Ctrl+Enter 发送，Enter 换行）", "メッセージを入力...  （Ctrl+Enter で送信、Enter で改行）", "Nachricht eingeben...  (Strg+Enter zum Senden, Enter für eine neue Zeile)"},
    {"Type a message...  (Enter to send, Shift+Enter for a new line)", "輸入訊息...  （Enter 傳送，Shift+Enter 換行）", "输入消息...  （Enter 发送，Shift+Enter 换行）", "メッセージを入力...  （Enter で送信、Shift+Enter で改行）", "Nachricht eingeben...  (Enter zum Senden, Umschalt+Enter für eine neue Zeile)"},
    {"Copy message", "複製訊息", "复制消息", "メッセージをコピー", "Nachricht kopieren"},
    {"Edit message", "編輯訊息", "编辑消息", "メッセージを編集", "Nachricht bearbeiten"},
    {"Delete message", "刪除訊息", "删除消息", "メッセージを削除", "Nachricht löschen"},
    {"Branch conversation", "分支對話", "分支对话", "会話を分岐", "Unterhaltung verzweigen"},
    {"Delete Message", "刪除訊息", "删除消息", "メッセージの削除", "Nachricht löschen"},
    {"Are you sure you want to delete this message?", "確定要刪除此訊息嗎？", "确定要删除此消息吗？", "このメッセージを削除してもよろしいですか？", "Möchten Sie diese Nachricht wirklich löschen?"},
    {"Delete This Message removes only it. Delete This and Following removes it and every message after it. This cannot be undone.", "“僅刪除此訊息”只刪除這一條；“刪除此訊息及之後的訊息”會刪除它及其之後的所有訊息。此操作無法撤銷。", "“仅删除此消息”只删除这一条；“删除此消息及之后的消息”会删除它及其之后的所有消息。此操作无法撤销。", "「このメッセージのみ削除」はこのメッセージだけを削除します。「このメッセージ以降を削除」はこれ以降のすべてのメッセージも削除します。この操作は取り消せません。", "„Diese Nachricht löschen“ entfernt nur sie. „Diese und alle folgenden löschen“ entfernt sie und jede Nachricht danach. Das kann nicht rückgängig gemacht werden."},
    {"Delete This Message", "僅刪除此訊息", "仅删除此消息", "このメッセージのみ削除", "Diese Nachricht löschen"},
    {"Delete This and Following", "刪除此訊息及之後的訊息", "删除此消息及之后的消息", "このメッセージ以降を削除", "Diese und alle folgenden löschen"},
    {"verbatim: %1/%2", "逐字：%1/%2", "逐字：%1/%2", "逐語: %1/%2", "wörtlich: %1/%2"},
    {"inactive", "非活動", "非活动", "非アクティブ", "inaktiv"},
    {"Message:", "訊息：", "消息：", "メッセージ：", "Nachricht:"},
    {"Update summarization from the conversation", "從對話更新摘要", "从对话更新摘要", "会話から要約を更新", "Zusammenfassung aus der Unterhaltung aktualisieren"},
    {"Send", "傳送", "发送", "送信", "Senden"},
    {"Shrink the composer", "縮小輸入框", "缩小输入框", "入力欄を縮小", "Eingabefeld verkleinern"},
    {"Expand the composer", "展開輸入框", "展开输入框", "入力欄を展開", "Eingabefeld vergrößern"},
    // Assistant picker.
    {"Assistants", "助手", "助手", "アシスタント", "Assistenten"},
    {"Choose an assistant to open, or create a new one.", "選擇要開啟的助手，或新建一個。", "选择要打开的助手，或新建一个。", "開くアシスタントを選択するか、新規作成します。", "Wählen Sie einen Assistenten zum Öffnen oder erstellen Sie einen neuen."},
    {"New assistant", "新建助手", "新建助手", "新規アシスタント", "Neuer Assistent"},
    {"Open automatically at startup", "啟動時自動開啟", "启动时自动打开", "起動時に自動的に開く", "Beim Start automatisch öffnen"},
    {"Untitled assistant", "未命名助手", "未命名助手", "無題のアシスタント", "Unbenannter Assistent"},
    {"No assistant selected", "未選擇助手", "未选择助手", "アシスタントが選択されていません", "Kein Assistent ausgewählt"},
    {"There are no assistants yet. Create one to get started.", "還沒有助手。新建一個即可開始。", "还没有助手。新建一个即可开始。", "アシスタントがまだありません。作成して始めましょう。", "Es gibt noch keine Assistenten. Erstellen Sie einen, um zu beginnen."},
    {"Select an assistant on the left to see it here.", "在左側選擇一個助手以在此檢視。", "在左侧选择一个助手以在此查看。", "左側のアシスタントを選択するとここに表示されます。", "Wählen Sie links einen Assistenten aus, um ihn hier anzuzeigen."},
    {"Edit assistant", "編輯助手", "编辑助手", "アシスタントを編集", "Assistent bearbeiten"},
    // Session editor.
    {"Edit Session", "編輯會話", "编辑会话", "セッションを編集", "Sitzung bearbeiten"},
    {"General", "通用", "通用", "一般", "Allgemein"},
    {"Summarization", "摘要", "摘要", "要約", "Zusammenfassung"},
    {"Memory preset", "記憶預設", "记忆默认", "メモリプリセット", "Speicher-Voreinstellung"},
    {"Summarization triggers", "摘要觸發", "摘要触发", "要約トリガー", "Auslöser der Zusammenfassung"},
    {"Auto compact reasoning", "摘要推理", "摘要推理", "要約の推論", "Reasoning automatisch verdichten"},
    {"The reasoning level used when this session's summarization is updated. Chat turns are unaffected.", "更新此會話摘要時使用的推理級別。聊天回合不受影響。", "更新此会话摘要时使用的推理级别。聊天回合不受影响。", "このセッションの要約が更新されるときに使用される推論レベル。チャットのターンには影響しません。", "Die Reasoning-Stufe, die beim Aktualisieren der Zusammenfassung dieser Sitzung verwendet wird. Chat-Turns sind nicht betroffen."},
    {"Session", "會話", "会话", "セッション", "Sitzung"},
    {"Session title", "會話標題", "会话标题", "セッションのタイトル", "Sitzungstitel"},
    {"Directories the assistant may access, one per line (optional)", "助手可訪問的目錄，每行一個（可選）", "助手可访问的目录，每行一个（可选）", "アシスタントがアクセスできるディレクトリ（1行に1つ、任意）", "Verzeichnisse, auf die der Assistent zugreifen darf, eines pro Zeile (optional)"},
    {"Title", "標題", "标题", "タイトル", "Titel"},
    {"Reasoning", "推理", "推理", "推論", "Reasoning"},
    {"The reasoning level this session requests for chat turns.", "此會話為聊天回合請求的推理級別。", "此会话为聊天回合请求的推理级别。", "このセッションがチャットのターンに要求する推論レベル。", "Die Reasoning-Stufe, die diese Sitzung für Chat-Turns anfordert."},
    {"Shared directories", "共享目錄", "共享目录", "共有ディレクトリ", "Gemeinsame Verzeichnisse"},
    {"One directory per line. The model only sees relative paths inside these directories, and its writes stay inside them. If several directories share a name, the later ones gain a numeric suffix.", "每行一個目錄。模型只能看到這些目錄內的相對路徑，其寫入也限於其中。若多個目錄同名，靠後的目錄會獲得數字字尾。", "每行一个目录。模型只能看到这些目录内的相对路径，其写入也限于其中。若多个目录同名，靠后的目录会获得数字后缀。", "1行に1つのディレクトリ。モデルはこれらのディレクトリ内の相対パスのみを認識し、書き込みもその中に限定されます。複数のディレクトリが同じ名前を持つ場合、後のものには数値の接尾辞が付きます。", "Ein Verzeichnis pro Zeile. Das Modell sieht nur relative Pfade innerhalb dieser Verzeichnisse, und seine Schreibzugriffe bleiben in ihnen. Wenn mehrere Verzeichnisse denselben Namen haben, erhalten die späteren ein numerisches Suffix."},
    {"Tool permissions", "工具許可權", "工具权限", "ツールの権限", "Tool-Berechtigungen"},
    {"Enable for this session", "為此會話啟用", "为此会话激活", "このセッションで有効にする", "Für diese Sitzung aktivieren"},
    {"When off, no tools are offered to the model for this session. The switches below keep their values.", "關閉時，不會向該會話的模型提供任何工具。下方的開關會保留其取值。", "关闭时，不会向该会话的模型提供任何工具。下方的开关会保留其取值。", "オフのとき、このセッションのモデルにはツールが提供されません。下のスイッチは値を保持します。", "Wenn deaktiviert, werden dem Modell in dieser Sitzung keine Tools angeboten. Die Schalter unten behalten ihre Werte."},
    {"Read files", "讀取檔案", "读取文件", "ファイルを読み取る", "Dateien lesen"},
    {"Allow listing, searching and reading files in the shared directories.", "允許在共享目錄中列出、搜尋和讀取檔案。", "允许在共享目录中列出、搜索和读取文件。", "共有ディレクトリ内のファイルの一覧表示、検索、読み取りを許可します。", "Auflisten, Durchsuchen und Lesen von Dateien in den gemeinsamen Verzeichnissen erlauben."},
    {"Write files", "寫入檔案", "写入文件", "ファイルに書き込む", "Dateien schreiben"},
    {"Allow creating, overwriting, editing and deleting files in the shared directories.", "允許在共享目錄中建立、覆蓋、編輯和刪除檔案。", "允许在共享目录中创建、覆盖、编辑和删除文件。", "共有ディレクトリ内のファイルの作成、上書き、編集、削除を許可します。", "Erstellen, Überschreiben, Bearbeiten und Löschen von Dateien in den gemeinsamen Verzeichnissen erlauben."},
    {"Search online", "線上搜尋", "在线搜索", "オンライン検索", "Online suchen"},
    {"Allow web searches and fetching web pages.", "允許搜尋網路並獲取網頁。", "允许搜索网络并获取网页。", "ウェブ検索とページの取得を許可します。", "Websuchen und das Abrufen von Webseiten erlauben."},
    {"Search endpoint", "搜尋端點", "搜索端点", "検索エンドポイント", "Such-Endpunkt"},
    {"Base URL of a SearXNG-compatible search service, e.g. the local browser bridge at http://127.0.0.1:8873. Leave empty to disable web search.", "相容 SearXNG 的搜尋服務的基礎 URL，例如位於 http://127.0.0.1:8873 的本地瀏覽器橋接。留空則禁用網路搜尋。", "兼容 SearXNG 的搜索服务的基础 URL，例如位于 http://127.0.0.1:8873 的本地浏览器桥接。留空则禁用网络搜索。", "SearXNG 互換の検索サービスのベース URL（例: http://127.0.0.1:8873 のローカルブラウザブリッジ）。空欄にするとウェブ検索を無効にします。", "Basis-URL eines SearXNG-kompatiblen Suchdienstes, z. B. der lokale Browser-Bridge unter http://127.0.0.1:8873. Leer lassen, um die Websuche zu deaktivieren."},
    {"Tools", "工具", "工具", "ツール", "Tools"},
    {"Read", "讀取", "读取", "読み取り", "Lesen"},
    {"Write", "寫入", "写入", "書き込み", "Schreiben"},
    {"Online", "線上", "在线", "オンライン", "Online"},
    {"Custom instructions", "自定義指令", "自定义指令", "カスタム指示", "Benutzerdefinierte Anweisungen"},
    {"Kept separate from the generated summary; never overwritten by it.", "與生成的摘要分開儲存；永遠不會被其覆蓋。", "与生成的摘要分开保存；永远不会被其覆盖。", "生成された要約とは別に保持され、上書きされることはありません。", "Wird getrennt von der generierten Zusammenfassung aufbewahrt und niemals von ihr überschrieben."},
    {"Merge the conversation into the summary below, ignoring the automatic summarization trigger. Idempotent: pressing it again with no new messages changes nothing.", "忽略自動摘要觸發閾值，將對話合併到下面的摘要中。冪等：沒有新訊息時再次按下不會改變任何內容。", "忽略自动摘要触发阈值，将对话合并到下面的摘要中。幂等：没有新消息时再次按下不会改变任何内容。", "自動要約トリガーを無視して、会話を下の要約に統合します。冪等：新しいメッセージがない状態で再度押しても何も変わりません。", "Führt die Unterhaltung in die untenstehende Zusammenfassung ein, ohne den automatischen Auslöser der Zusammenfassung zu berücksichtigen. Idempotent: Ein erneutes Drücken ohne neue Nachrichten ändert nichts."},
    {"Include in assistant memory", "包含在助手記憶中", "包含在助手记忆中", "アシスタントメモリに含める", "In den Assistenten-Speicher aufnehmen"},
    {"When off, this session's messages and summarization are left out of the shared assistant memory. This session's own summarization is unaffected.", "關閉時，此會話的訊息和摘要不會進入共享的助手記憶。此會話自身的摘要不受影響。", "关闭时，此会话的消息和摘要不会进入共享的助手记忆。此会话自身的摘要不受影响。", "オフのとき、このセッションのメッセージと要約は共有アシスタントメモリに含まれません。このセッション自身の要約は影響を受けません。", "Wenn deaktiviert, bleiben die Nachrichten und die Zusammenfassung dieser Sitzung aus dem gemeinsamen Assistenten-Speicher heraus. Die eigene Zusammenfassung dieser Sitzung ist nicht betroffen."},
    {"Automatically maintained record of older turns. Edit to correct it, or clear it to rebuild it from the whole transcript.", "自動維護的較早輪次記錄。可編輯以修正，或清空以從完整對話記錄重新構建。", "自动维护的较早轮次记录。可编辑以修正，或清空以从完整对话记录重新构建。", "古いやり取りの自動維持される記録です。編集して修正するか、消去してトランスクリプト全体から再構築してください。", "Automatisch gepflegte Aufzeichnung älterer Turns. Bearbeiten Sie sie zur Korrektur oder leeren Sie sie, um sie aus dem gesamten Verlauf neu aufzubauen."},
    {"Conversation summary position: the highest message id already merged into the summary above. Used only to decide when the automatic summarization update fires.", "對話摘要位置：已合併到上方摘要中的最大訊息 ID。僅用於判斷自動摘要更新何時發生。", "对话摘要位置：已合并到上方摘要中的最大消息 ID。仅用于判断自动摘要更新何时发生。", "会話要約位置：上の要約に既に統合された最大メッセージ ID。自動要約更新の発火時期を判断するためにのみ使用されます。", "Position der Unterhaltungszusammenfassung: die höchste Nachrichten-ID, die bereits in die obige Zusammenfassung eingeflossen ist. Wird nur verwendet, um zu entscheiden, wann die automatische Aktualisierung der Zusammenfassung ausgelöst wird."},
    {"Assistant memory position for this session: the highest message id already merged into the shared assistant memory.", "此會話的助手記憶位置：已合併到共享助手記憶中的最大訊息 ID。", "此会话的助手记忆位置：已合并到共享助手记忆中的最大消息 ID。", "このセッションのアシスタントメモリ位置：共有アシスタントメモリに既に統合された最大メッセージ ID。", "Position des Assistenten-Speichers für diese Sitzung: die höchste Nachrichten-ID, die bereits in den gemeinsamen Assistenten-Speicher eingeflossen ist."},
    {"Summarization watermark", "摘要水位", "摘要水位", "要約ウォーターマーク", "Wasserzeichen der Zusammenfassung"},
    {"Assistant watermark", "助手水位", "助手水位", "アシスタントウォーターマーク", "Wasserzeichen des Assistenten"},
    {"Stored summary versions, newest first. Select one to view its summary and watermark; right-click one to remove it.", "已儲存的摘要版本，最新的在前。選擇一個以檢視其摘要和水位；右鍵單擊可移除。", "已保存的摘要版本，最新的在前。选择一个以查看其摘要和水位；右键单击可移除。", "保存された要約バージョン（新しい順）。選択すると要約とウォーターマークが表示されます。右クリックで削除できます。", "Gespeicherte Zusammenfassungsversionen, neueste zuerst. Wählen Sie eine aus, um ihre Zusammenfassung und ihr Wasserzeichen anzuzeigen; klicken Sie mit der rechten Maustaste darauf, um sie zu entfernen."},
    {"Message %1", "訊息 %1", "消息 %1", "メッセージ %1", "Nachricht %1"},
    {"Active", "當前", "当前", "有効", "Aktiv"},
    {"Remove summary version", "移除摘要版本", "移除摘要版本", "要約バージョンを削除", "Zusammenfassungsversion entfernen"},
    {"Merged up to message %1, made at %2.", "已合併至訊息 %1，建立於 %2。", "已合并至消息 %1，创建于 %2。", "メッセージ %1 まで統合、作成日時 %2。", "Bis Nachricht %1 zusammengeführt, erstellt am %2."},
    {"Budgets", "預算", "预算", "予算", "Budgets"},
    {"Tool limits", "工具限制", "工具限制", "ツールの制限", "Tool-Limits"},
    {"Apply a preset...", "應用預設...", "应用默认...", "プリセットを適用...", "Voreinstellung anwenden..."},
    {"Preset", "預設", "默认", "プリセット", "Voreinstellung"},
    {"Fill the summarization fields below with a preset. It also sets the response budget on the General page, which must not exceed the summarization trigger. Nothing is saved until you press Save.", "使用預設填充下面的摘要欄位。它還會設定“通用”頁面中的回覆預算，該預算不得大於摘要觸發閾值。在你按下“儲存”之前不會儲存任何內容。", "使用默认填充下面的摘要字段。它还会设置“通用”页面中的回复预算，该预算不得大于摘要触发阈值。在你按下“保存”之前不会保存任何内容。", "プリセットで下の要約項目を入力します。「一般」ページの応答バジェットも設定されます（要約トリガーを超えてはいけません）。「保存」を押すまで何も保存されません。", "Füllen Sie die Zusammenfassungsfelder unten mit einer Voreinstellung. Sie legt außerdem das Antwort-Budget auf der Seite „Allgemein“ fest, das den Auslöser der Zusammenfassung nicht überschreiten darf. Nichts wird gespeichert, bis Sie auf Speichern klicken."},
    {"Fill the tool limits below with a preset. Nothing is saved until you press Save.", "使用預設填充下面的工具限制。在你按下“儲存”之前不會儲存任何內容。", "使用默认填充下面的工具限制。在你按下“保存”之前不会保存任何内容。", "プリセットで下のツール制限を入力します。「保存」を押すまで何も保存されません。", "Füllen Sie die Tool-Limits unten mit einer Voreinstellung. Nichts wird gespeichert, bis Sie auf Speichern klicken."},
    {"Summarization trigger", "摘要觸發閾值", "摘要触发阈值", "要約トリガー", "Auslöser der Zusammenfassung"},
    {"Summarization content", "摘要內容", "摘要内容", "要約内容", "Zusammenfassungsinhalt"},
    {"Memory budget", "記憶預算", "记忆预算", "メモリバジェット", "Speicher-Budget"},
    {"Summarization budget", "摘要預算", "摘要预算", "要約バジェット", "Zusammenfassungs-Budget"},
    {"Response budget", "回覆預算", "回复预算", "応答バジェット", "Antwort-Budget"},
    {"Input token produced midway", "中途產生的輸入 token 數", "中途产生的输入 token 数", "途中で生成される入力トークン数", "Mittendrin erzeugte Eingabe-Token"},
    {"Total tokens of tool output this session may feed back to the model in one turn, counted across every tool call in that turn. Once the budget is spent, the remaining answer is produced without tools. 0 disables the limit.", "此會話在一輪對話中最多可回傳給模型的工具輸出 token 總數，按該輪所有工具呼叫累計。預算用盡後，剩餘的回覆將在不啟用工具的情況下生成。0 表示不限制。", "此会话在一轮对话中最多可回传给模型的工具输出 token 总数，按该轮所有工具调用累计。预算用尽后，剩余的回复将在不激活工具的情况下生成。0 表示不限制。", "このセッションが 1 ターンでモデルに返せるツール出力のトークン総数です。そのターンのすべてのツール呼び出しを合計します。バジェットを使い切ると、残りの応答はツールなしで生成されます。0 は制限なしを意味します。", "Gesamtzahl der Token von Tool-Ausgaben, die diese Sitzung dem Modell in einem Turn zurückmelden darf, gezählt über alle Tool-Aufrufe in diesem Turn. Ist das Budget aufgebraucht, wird die restliche Antwort ohne Tools erzeugt. 0 deaktiviert das Limit."},
    {"Estimated input: %1 tokens", "預計輸入：%1 token", "预计输入：%1 token", "推定入力：%1 トークン", "Geschätzte Eingabe: %1 Token"},
    {"Approximate model-facing size of the request, using the app's character-based token estimate.", "請求的大致模型輸入規模，使用應用基於字元的 token 估算。", "请求的大致模型输入规模，使用应用基于字符的 token 估算。", "リクエストのモデル向け入力サイズの概算です。アプリの文字ベースのトークン推定を使用します。", "Ungefähre modellseitige Größe der Anfrage, basierend auf der zeichenbasierten Token-Schätzung der App."},
    {"Invalid Budget", "預算無效", "预算无效", "無効なバジェット", "Ungültiges Budget"},
    {"The summarization content must be smaller than the summarization trigger. Otherwise an update would find nothing new to fold, and the trigger would fire again immediately.", "摘要內容必須小於摘要觸發閾值。否則更新時沒有可摺疊的新內容，觸發閾值會立即再次觸發。", "摘要内容必须小于摘要触发阈值。否则更新时没有可折叠的新内容，触发阈值会立即再次触发。", "要約内容は要約トリガーより小さくなければなりません。そうでないと折り畳む新しい内容がなくなり、トリガーが直ちに再発火します。", "Der Zusammenfassungsinhalt muss kleiner als der Auslöser der Zusammenfassung sein. Andernfalls fände eine Aktualisierung nichts Neues zum Einfalten, und der Auslöser würde sofort erneut ausgelöst."},
    {"The response budget must not be greater than the summarization trigger. A reply could otherwise never complete before the conversation is merged again.", "回覆預算不得大於摘要觸發閾值。否則回覆可能永遠無法在對話再次合併之前完成。", "回复预算不得大于摘要触发阈值。否则回复可能永远无法在对话再次合并之前完成。", "応答バジェットは要約トリガー以下でなければなりません。そうでないと、会話が再び統合される前に返信が完了できない可能性があります。", "Das Antwort-Budget darf nicht größer als der Auslöser der Zusammenfassung sein. Andernfalls könnte eine Antwort nie vollständig sein, bevor die Unterhaltung erneut zusammengeführt wird."},
    {"<b>How conversation summarization works</b><br>Conversation summarization is a rolling record of older turns. A request carries the system prompt plus the transcript, always the newest messages that fit <i>Summarization trigger</i>. When the new transcript since the last update reaches that trigger, the summarizer reads from the first new message up to the newest <i>Summarization content</i> tokens and merges them into the summary. Keep it below the trigger so there is always something to fold. The message that crosses the boundary is kept whole.<br><br>The watermarks above are <b>bookkeeping only</b>: they count how much is new so the trigger can fire. They never decide what is sent or what is read — a summarization update always reads by recency, so pressing <b>Update summarization now</b> twice with no new messages reads the same set and produces the same summary (idempotent). Custom instructions are kept separate and are never touched.<br><br>The <b>assistant memory</b> is shared across every session: it is merged from the recent messages of all sessions (bounded by the assistant's own trigger) plus each session's summarization (bounded by the session-pool budget). Older sessions may therefore be forgotten. This session opts out of that sharing when <i>Include in assistant memory</i> is off.<br><br>Clearing the summary and saving resets its watermark to 0, re-admitting the whole transcript.", "<b>對話摘要的工作原理</b><br>對話摘要是較早輪次的滾動記錄。每次請求都會攜帶系統提示詞以及對話記錄，並且總是包含能放入 <i>摘要觸發閾值</i> 的最新訊息。當自上次更新以來的新對話達到該閾值時，摘要器會從第一條新訊息讀到最新的 <i>摘要內容</i> token 處，並將它們合併進摘要中。請讓它小於觸發閾值，以便總有內容可摺疊。跨越邊界的訊息會完整保留。<br><br>上方的水位<b>僅用於記帳</b>：它們統計有多少新內容，以便觸發閾值能夠觸發。它們絕不決定傳送或讀取什麼——摘要更新始終按最近程度讀取，因此在沒有新訊息時按兩次 <b>立即更新摘要</b> 會讀取同一組內容並產生相同的摘要（冪等）。自定義指令單獨儲存，永遠不會被觸碰。<br><br><b>助手記憶</b>在所有會話之間共享：它由所有會話的最近訊息（受助手自身觸發閾值限制）加上每個會話的摘要（受會話池預算限制）合併而成。因此較早的會話可能會被遺忘。關閉 <i>包含在助手記憶中</i> 時，此會話不參與該共享。<br><br>清空摘要並儲存會將其水位重置為 0，從而重新納入整個對話記錄。", "<b>对话摘要的工作原理</b><br>对话摘要是较早轮次的滚动记录。每次请求都会携带系统提示词以及对话记录，并且总是包含能放入 <i>摘要触发阈值</i> 的最新消息。当自上次更新以来的新对话达到该阈值时，摘要器会从第一条新消息读到最新的 <i>摘要内容</i> token 处，并将它们合并进摘要中。请让它小于触发阈值，以便总有内容可折叠。跨越边界的消息会完整保留。<br><br>上方的水位<b>仅用于记帐</b>：它们统计有多少新内容，以便触发阈值能够触发。它们绝不决定发送或读取什么——摘要更新始终按最近程度读取，因此在没有新消息时按两次 <b>立即更新摘要</b> 会读取同一组内容并产生相同的摘要（幂等）。自定义指令单独保存，永远不会被触碰。<br><br><b>助手记忆</b>在所有会话之间共享：它由所有会话的最近消息（受助手自身触发阈值限制）加上每个会话的摘要（受会话池预算限制）合并而成。因此较早的会话可能会被遗忘。关闭 <i>包含在助手记忆中</i> 时，此会话不参与该共享。<br><br>清空摘要并保存会将其水位重置为 0，从而重新纳入整个对话记录。", "<b>会話要約の仕組み</b><br>会話要約は古いやり取りのローリング記録です。リクエストにはシステムプロンプトとトランスクリプトが含まれ、常に <i>要約トリガー</i> に収まる最新のメッセージが送信されます。前回の更新以降の新しいトランスクリプトがそのトリガーに達すると、要約器は最初の新しいメッセージから最新の <i>要約内容</i> トークンまでを読み取り、要約に統合します。トリガーより小さくしておくと、常に折り畳む内容があります。境界を越えるメッセージはそのまま保持されます。<br><br>上のウォーターマークは<b>記帳専用</b>です。どれだけ新しいか数えてトリガーを発火させるためだけに使用されます。何を送信し何を読み取るかを決定することはありません。要約の更新は常に新しい順に読み取るため、新しいメッセージがない状態で <b>今すぐ要約を更新</b> を2回押しても同じ集合を読み取り、同じ要約を生成します（冪等）。カスタム指示は別に保持され、決して変更されません。<br><br><b>アシスタントメモリ</b>はすべてのセッションで共有されます。これはすべてのセッションの最近のメッセージ（アシスタント自身のトリガーで上限）と各セッションの要約（セッションプールのバジェットで上限）から統合されます。そのため、古いセッションは忘れられる可能性があります。<i>アシスタントメモリに含める</i>がオフのとき、このセッションはその共有から外れます。<br><br>要約を消去して保存すると、ウォーターマークが 0 にリセットされ、トランスクリプト全体が再び取り込まれます。", "<b>So funktioniert die Unterhaltungszusammenfassung</b><br>Die Unterhaltungszusammenfassung ist eine fortlaufende Aufzeichnung älterer Turns. Eine Anfrage enthält den System-Prompt plus den Verlauf, und zwar stets die neuesten Nachrichten, die in den <i>Auslöser der Zusammenfassung</i> passen. Wenn der neue Verlauf seit der letzten Aktualisierung diesen Auslöser erreicht, liest der Summarizer von der ersten neuen Nachricht bis zu den neuesten <i>Zusammenfassungsinhalt</i>-Token und führt sie in die Zusammenfassung ein. Halten Sie ihn unter dem Auslöser, damit immer etwas zum Einfalten vorhanden ist. Die Nachricht, die die Grenze überschreitet, bleibt vollständig erhalten.<br><br>Die Wasserzeichen oben dienen <b>nur der Buchführung</b>: Sie zählen, wie viel neu ist, damit der Auslöser ausgelöst werden kann. Sie entscheiden nie darüber, was gesendet oder gelesen wird – eine Aktualisierung der Zusammenfassung liest immer nach Aktualität; zweimaliges Drücken von <b>Zusammenfassung jetzt aktualisieren</b> ohne neue Nachrichten liest also dieselbe Menge und erzeugt dieselbe Zusammenfassung (idempotent). Benutzerdefinierte Anweisungen werden getrennt aufbewahrt und nie berührt.<br><br>Der <b>Assistenten-Speicher</b> wird sitzungsübergreifend geteilt: Er wird aus den aktuellen Nachrichten aller Sitzungen (begrenzt durch den eigenen Auslöser des Assistenten) sowie der Zusammenfassung jeder Sitzung (begrenzt durch das Budget des Sitzungs-Pools) zusammengeführt. Ältere Sitzungen können daher vergessen werden. Diese Sitzung nimmt an dieser Weitergabe nicht teil, wenn <i>In den Assistenten-Speicher aufnehmen</i> deaktiviert ist.<br><br>Das Leeren der Zusammenfassung und anschließendes Speichern setzt ihr Wasserzeichen auf 0 zurück und lässt den gesamten Verlauf wieder zu."},
    {"Session Busy", "會話繁忙", "会话繁忙", "セッションはビジーです", "Sitzung ausgelastet"},
    {"A task is already running. Wait for it to finish, then update the summarization.", "已有任務正在執行。請等待其完成，然後更新摘要。", "已有任务正在运行。请等待其完成，然后更新摘要。", "タスクが既に実行中です。完了を待ってから要約を更新してください。", "Es läuft bereits eine Aufgabe. Warten Sie, bis sie abgeschlossen ist, und aktualisieren Sie dann die Zusammenfassung."},
    // Presets.
    {"Instant", "極輕", "极轻", "最軽量", "Sofort"},
    {"Very Light", "極輕", "极轻", "最軽量", "Sehr leicht"},
    {"Light", "輕量", "轻量", "軽量", "Leicht"},
    {"Balanced", "均衡", "均衡", "バランス", "Ausgewogen"},
    {"Heavy", "重度", "重度", "ヘビー", "Stark"},
    {"Very Heavy", "極重", "极重", "超ヘビー", "Sehr stark"},
    {"Ultra", "極致", "极致", "ウルトラ", "Ultra"},
    // Assistant form.
    {"Assistant name", "助手名稱", "助手名称", "アシスタント名", "Name des Assistenten"},
    {"Model id, e.g. gpt-4o", "模型 ID，例如 gpt-4o", "模型 ID，例如 gpt-4o", "モデル ID（例: gpt-4o）", "Modell-ID, z. B. gpt-4o"},
    {"Name", "名稱", "名称", "名前", "Name"},
    {"Model", "模型", "模型", "モデル", "Modell"},
    {"Connection", "連線", "连接", "接続", "Verbindung"},
    {"https://api.example.com/v1/chat/completions", "https://api.example.com/v1/chat/completions", "https://api.example.com/v1/chat/completions", "https://api.example.com/v1/chat/completions", "https://api.example.com/v1/chat/completions"},
    {"https://api.example.com/v1", "https://api.example.com/v1", "https://api.example.com/v1", "https://api.example.com/v1", "https://api.example.com/v1"},
    {"Authentication token", "身份驗證令牌", "身份验证令牌", "認証トークン", "Authentifizierungs-Token"},
    {"Optional", "可選", "可选", "任意", "Optional"},
    {"API URL", "API URL", "API URL", "API URL", "API-URL"},
    {"Token", "令牌", "令牌", "トークン", "Token"},
    {"Proxy", "代理", "代理", "プロキシ", "Proxy"},
    {"Turn timeout", "回合超時", "回合超时", "ターンタイムアウト", "Turn-Timeout"},
    {"Applies to a new session's starting level and to assistant-wide memory updates, the shared memory across sessions. Chat turns and session summarization updates use the session's own level instead.", "適用於新會話的起始級別，以及助手級記憶更新（跨會話共享的記憶）。聊天回合和會話級摘要更新改用會話自身的級別。", "适用于新会话的起始级别，以及助手级记忆更新（跨会话共享的记忆）。聊天回合和会话级摘要更新改用会话自身的级别。", "新しいセッションの開始レベルと、アシスタント全体のメモリ更新（セッション間で共有されるメモリ）に適用されます。チャットのターンとセッション単位の要約更新は、セッション自身のレベルを使用します。", "Gilt für das Startniveau einer neuen Sitzung und für sitzungsübergreifende Gedächtnis-Updates des Assistenten, das gemeinsame Gedächtnis über alle Sitzungen. Chat-Runden und das Zusammenfassen von Sitzungen verwenden stattdessen das Niveau der jeweiligen Sitzung."},
    {"Memory update timeout", "記憶更新超時", "记忆更新超时", "メモリ更新タイムアウト", "Zeitlimit für Gedächtnis-Update"},
    {"Seconds a request may wait for the next response byte before it is abandoned. Chat turns use the turn timeout; session summarization and assistant memory updates use the memory update timeout. Connecting and uploading keep a fixed 60-second window.", "請求在放棄前等待下一個響應位元組的秒數。聊天回合使用回合超時；會話摘要和助手記憶更新使用記憶更新超時。連線和上傳保持固定的 60 秒視窗。", "请求在放弃前等待下一个响应字节的秒数。聊天回合使用回合超时；会话摘要和助手记忆更新使用记忆更新超时。连接和上传保持固定的 60 秒窗口。", "リクエストが破棄されるまでに次の応答バイトを待つ秒数。チャットのターンはターンタイムアウトを、セッションの要約およびアシスタントのメモリ更新はメモリ更新タイムアウトを使用します。接続とアップロードは固定の 60 秒ウィンドウのままです。", "Sekunden, die eine Anfrage auf das nächste Antwort-Byte warten darf, bevor sie abgebrochen wird. Chat-Runden nutzen das Zeitlimit für Runden; das Zusammenfassen von Sitzungen und Gedächtnis-Updates des Assistenten nutzen das Zeitlimit für Gedächtnis-Updates. Verbinden und Hochladen behalten ein festes Zeitfenster von 60 Sekunden."},
    {"Maximum silence, in seconds, while a chat turn streams its reply.", "聊天回合流式回覆時允許的最長靜默秒數。", "聊天回合流式回复时允许的最长静默秒数。", "チャットのターンが応答をストリーミングする際に許容される最大の無音秒数。", "Maximale Stille in Sekunden, während eine Chat-Runde ihre Antwort streamt."},
    {"Maximum silence, in seconds, while a session summarization or assistant memory update runs. Reasoning models often need longer.", "會話摘要或助手記憶更新執行期間允許的最長靜默秒數。推理模型通常需要更長時間。", "会话摘要或助手记忆更新运行期间允许的最长静默秒数。推理模型通常需要更长时间。", "セッションの要約またはアシスタントのメモリ更新の実行中に許容される最大の無音秒数。推論モデルではより長く必要になることがよくあります。", "Maximale Stille in Sekunden, während eine Sitzungs-Zusammenfassung oder ein Gedächtnis-Update des Assistenten läuft. Reasoning-Modelle brauchen oft länger."},
    {"System prompt for this assistant", "此助手的系統提示詞", "此助手的系统提示词", "このアシスタントのシステムプロンプト", "System-Prompt für diesen Assistenten"},
    {"Fill the memory trigger and budget fields below with a preset. Nothing is saved until you press Save.", "使用預設填充下面的記憶觸發閾值與預算欄位。在你按下“儲存”之前不會儲存任何內容。", "使用默认填充下面的记忆触发阈值与预算字段。在你按下“保存”之前不会保存任何内容。", "プリセットで下のメモリトリガーとバジェットの項目を入力します。「保存」を押すまで何も保存されません。", "Füllen Sie die Felder für Gedächtnis-Auslöser und Budget unten mit einer Voreinstellung. Nichts wird gespeichert, bis Sie auf Speichern klicken."},
    {"Target size of the generated assistant memory.", "生成的助手記憶的目標大小。", "生成的助手记忆的目标大小。", "生成されるアシスタントメモリの目標サイズ。", "Zielgröße des generierten Assistenten-Gedächtnisses."},
    {"Extra completion budget for a reasoning model's hidden thinking, so the visible memory is not starved.", "為推理模型的隱藏思考預留的額外補全預算，以免可見記憶被擠佔。", "为推理模型的隐藏思考预留的额外补全预算，以免可见记忆被挤占。", "推論モデルの隠れた思考のための追加の補完バジェット。可視メモリが逼迫しないようにします。", "Zusätzliches Completion-Budget für das verborgene Denken eines Reasoning-Modells, damit das sichtbare Gedächtnis nicht zu kurz kommt."},
    {"New tokens across all sessions that trigger a memory update; also the cap on the recent messages gathered.", "所有會話中觸發記憶更新的新 token 總數；同時也是所收集最近訊息的上限。", "所有会话中触发记忆更新的新 token 总数；同时也是所收集最近消息的上限。", "メモリの更新を起動する全セッションの新規トークン数。収集される最近のメッセージの上限でもあります。", "Neue Token über alle Sitzungen hinweg, die ein Gedächtnis-Update auslösen; zugleich die Obergrenze für die gesammelten letzten Nachrichten."},
    {"Token cap on the per-session summarizations included in the assistant memory.", "包含在助手記憶中的每個會話摘要的 token 上限。", "包含在助手记忆中的每个会话摘要的 token 上限。", "アシスタントメモリに含まれるセッションごとの要約のトークン上限。", "Token-Obergrenze für die sitzungsbezogenen Zusammenfassungen, die ins Assistenten-Gedächtnis einfließen."},

    {"Session pool", "會話池", "会话池", "セッションプール", "Sitzungspool"},
    // Tasks window.
    {"%1 s", "%1 秒", "%1 秒", "%1 秒", "%1 s"},
    {"This task produced no log.", "此任務未生成日誌。", "此任务未生成日志。", "このタスクはログを生成しませんでした。", "Für diese Aufgabe wurde kein Protokoll erzeugt."},
    {"Select a task to read its log.", "選擇一個任務以檢視其日誌。", "选择一个任务以查看其日志。", "ログを確認するタスクを選択してください。", "Wählen Sie eine Aufgabe, um ihr Protokoll zu lesen."},
    {"Copy log", "複製日誌", "复制日志", "ログをコピー", "Protokoll kopieren"},
    {"Request", "請求", "请求", "リクエスト", "Anfrage"},
    {"Response", "響應", "响应", "応答", "Antwort"},
    {"Tool call", "工具呼叫", "工具调用", "ツール呼び出し", "Tool-Aufruf"},
    {"Tool result", "工具結果", "工具结果", "ツール結果", "Tool-Ergebnis"},
    {"Reasoning", "推理", "推理", "推論", "Reasoning"},
    {"Failure", "失敗", "失败", "失敗", "Fehler"},
    {"Log", "日誌", "日志", "ログ", "Protokoll"},
    {"Round", "輪次", "轮次", "ラウンド", "Runde"},
    {"%1 characters", "%1 個字元", "%1 个字符", "%1 文字", "%1 Zeichen"},
    {"Model: %1", "模型：%1", "模型：%1", "モデル: %1", "Modell: %1"},
    {"Tasks", "任務", "任务", "タスク", "Aufgaben"},
    {"Running and recently finished generation tasks. Select one to read its log.", "正在執行和最近完成的生成任務。選擇一項以檢視其日誌。", "正在运行和最近完成的生成任务。选择一项以查看其日志。", "実行中および最近完了した生成タスク。項目を選択するとログを確認できます。", "Laufende und kürzlich abgeschlossene Generierungsaufgaben. Wählen Sie eine aus, um ihr Protokoll zu lesen."},
    {"No tasks yet.", "暫無任務。", "暂无任务。", "タスクはまだありません。", "Noch keine Aufgaben."},
    {"%1   ·   Round %2   ·   %3", "%1   ·   第 %2 輪   ·   %3", "%1   ·   第 %2 轮   ·   %3", "%1   ·   ラウンド %2   ·   %3", "%1   ·   Runde %2   ·   %3"},
    {"Input %1   ·   Output %2", "輸入 %1   ·   輸出 %2", "输入 %1   ·   输出 %2", "入力 %1   ·   出力 %2", "Eingabe %1   ·   Ausgabe %2"},
    {"response budget", "回覆預算", "回复预算", "応答バジェット", "Antwortbudget"},
    {"summarization budget", "摘要預算", "摘要预算", "要約バジェット", "Zusammenfassungsbudget"},
    {"memory budget", "記憶預算", "记忆预算", "メモリバジェット", "Gedächtnisbudget"},
    {", charged against the %1", "，計入%1", "，计入%1", "（%1に計上）", ", verrechnet mit dem %1"},
    {"Input: prompt tokens billed by every request of this task, summed over its tool rounds. Output: tokens generated plus the tool results fed back%1. Tool output is always counted at an estimated cost; a leading ~ marks a task whose request usage was estimated.", "輸入：此任務的每次請求所計費的提示詞 token，按工具輪次累計。輸出：生成的 token 加上回傳的工具結果%1。工具輸出始終按估算成本計算；開頭的 ~ 表示請求用量為本地估算的任務。", "输入：此任务的每次请求所计费的提示词 token，按工具轮次累计。输出：生成的 token 加上回传的工具结果%1。工具输出始终按估算成本计算；开头的 ~ 表示请求用量为本地估算的任务。", "入力：このタスクの各リクエストで課金されたプロンプトトークン（ツールラウンド分を合計）。出力：生成されたトークンと返されたツール結果%1。ツール出力は常に推定コストで計上されます。先頭の ~ はリクエストの使用量が推定されたタスクを示します。", "Eingabe: Prompt-Token, die jede Anfrage dieser Aufgabe in Rechnung stellt, summiert über ihre Tool-Runden. Ausgabe: generierte Token plus die zurückgegebenen Tool-Ergebnisse%1. Tool-Ausgaben werden immer mit geschätzten Kosten gezählt; eine führende ~ kennzeichnet eine Aufgabe, deren Anfrageverbrauch geschätzt wurde."},
    // Summary review dialog.
    {"Review summarization update", "審查摘要更新", "审查摘要更新", "要約更新の確認", "Zusammenfassungs-Update prüfen"},
    {"The following request is about to be sent to the model to update the %1. Review it and choose whether to run the summarization update.", "以下請求即將傳送給模型以更新 %1。請審閱，並選擇是否執行摘要更新。", "以下请求即将发送给模型以更新 %1。请审阅，并选择是否运行摘要更新。", "以下のリクエストが %1 を更新するためにモデルへ送信されようとしています。確認して、要約の更新を実行するか選択してください。", "Die folgende Anfrage wird gleich an das Modell gesendet, um das %1 zu aktualisieren. Prüfen Sie sie und wählen Sie, ob das Zusammenfassungs-Update ausgeführt werden soll."},
    {"This is the first of several requests: the remaining messages are merged in further batches after this one.", "這是多個請求中的第一個：其餘訊息將在本次之後分批進一步合併。", "这是多个请求中的第一个：其余消息将在本次之后分批进一步合并。", "これは複数のリクエストの最初のものです。残りのメッセージはこの後さらにバッチで統合されます。", "Dies ist die erste von mehreren Anfragen: Die übrigen Nachrichten werden in weiteren Stapeln nach dieser zusammengeführt."},
    {"Summarization Update Skipped", "已跳過摘要更新", "已跳过摘要更新", "要約の更新をスキップしました", "Zusammenfassungs-Update übersprungen"},
    {"The automatic summarization update was skipped. Some messages may not be merged into the summary; use Update summarization now before sending new messages if you want them included.", "自動摘要更新已跳過。部分訊息可能尚未合併到摘要中；若希望它們被包含，請在傳送新訊息前使用“立即更新摘要”。", "自动摘要更新已跳过。部分消息可能尚未合并到摘要中；若希望它们被包含，请在发送新消息前使用“立即更新摘要”。", "自動要約更新がスキップされました。一部のメッセージが要約に統合されていない可能性があります。含めたい場合は、新しいメッセージを送信する前に「今すぐ要約を更新」を使用してください。", "Das automatische Zusammenfassungs-Update wurde übersprungen. Einige Nachrichten wurden möglicherweise nicht in die Zusammenfassung übernommen; verwenden Sie „Zusammenfassung jetzt aktualisieren“, bevor Sie neue Nachrichten senden, wenn sie enthalten sein sollen."},
    {"Model: %1\n", "模型：%1\n", "模型：%1\n", "モデル: %1\n", "Modell: %1\n"},
    {"Completion cap: %1 tokens\n\n", "補全上限：%1 個 token\n\n", "补全上限：%1 个 token\n\n", "補完上限: %1 トークン\n\n", "Completion-Obergrenze: %1 Token\n\n"},
    // Tool status.
    {"Reading files...", "正在讀取檔案...", "正在读取文件...", "ファイルを読み込み中...", "Dateien werden gelesen..."},
    {"Searching code...", "正在搜尋程式碼...", "正在搜索代码...", "コードを検索中...", "Code wird durchsucht..."},
    {"Listing files...", "正在列出檔案...", "正在列出文件...", "ファイルを一覧表示中...", "Dateien werden aufgelistet..."},
    {"Finding files...", "正在查詢檔案...", "正在查找文件...", "ファイルを検索中...", "Dateien werden gesucht..."},
    {"Writing file...", "正在寫入檔案...", "正在写入文件...", "ファイルに書き込み中...", "Datei wird geschrieben..."},
    {"Editing file...", "正在編輯檔案...", "正在编辑文件...", "ファイルを編集中...", "Datei wird bearbeitet..."},
    {"Deleting file...", "正在刪除檔案...", "正在删除文件...", "ファイルを削除中...", "Datei wird gelöscht..."},
    {"Fetching page...", "正在獲取頁面...", "正在获取页面...", "ページを取得中...", "Seite wird abgerufen..."},
    {"Searching the web...", "正在搜尋網路...", "正在搜索网络...", "ウェブを検索中...", "Web wird durchsucht..."},
    {"Using tools...", "正在使用工具...", "正在使用工具...", "ツールを使用中...", "Tools werden verwendet..."},
    // Log inspector.
    {"Logs", "日誌", "日志", "ログ", "Protokolle"},
    {"Info", "資訊", "信息", "情報", "Info"},
    {"Debug", "除錯", "调试", "デバッグ", "Debug"},
    {"Error", "錯誤", "错误", "エラー", "Fehler"},
    {"Level", "級別", "级别", "レベル", "Ebene"},
    {"All levels", "所有級別", "所有级别", "すべてのレベル", "Alle Ebenen"},
    {"Live records from the running application.", "執行中應用的實時記錄。", "运行中应用的实时记录。", "実行中アプリケーションのライブ記録。", "Live-Einträge aus der laufenden Anwendung."},
    {"No log records yet.", "暫無日誌記錄。", "暂无日志记录。", "ログ記録はまだありません。", "Noch keine Protokolleinträge."},
    {"Select a log entry to read it.", "選擇一條日誌以檢視。", "选择一条日志以查看。", "ログ項目を選択して表示します。", "Wählen Sie einen Protokolleintrag, um ihn zu lesen."},
    {"Copy entry", "複製條目", "复制条目", "項目をコピー", "Eintrag kopieren"},
    {"Clear", "清空", "清空", "クリア", "Leeren"},
    {"%1 records", "%1 條記錄", "%1 条记录", "%1 件の記録", "%1 Einträge"},
    {"%1   ·   %2", "%1   ·   %2", "%1   ·   %2", "%1   ·   %2", "%1   ·   %2"},
    // Log inspector: rendered request payloads.
    {"Payload", "載荷", "载荷", "ペイロード", "Payload"},
    {"LLM request", "LLM 請求", "LLM 请求", "LLM リクエスト", "LLM-Anfrage"},
    {"model", "模型", "模型", "モデル", "Modell"},
    {"items", "項", "项", "項目", "Elemente"},
    {"Messages", "訊息", "消息", "メッセージ", "Nachrichten"},
    {"Parameters", "引數", "参数", "パラメーター", "Parameter"},
    {"(empty)", "（空）", "（空）", "（空）", "(leer)"},
    {"(none)", "（無）", "（无）", "（なし）", "(keine)"},
    {"tool call", "工具呼叫", "工具调用", "ツール呼び出し", "Tool-Aufruf"},
    {"tool output", "工具輸出", "工具输出", "ツール出力", "Tool-Ausgabe"},
    {"reasoning", "推理", "推理", "推論", "Reasoning"},
    {"hidden", "已隱藏", "已隐藏", "非表示", "verborgen"},
    {"%1 input", "%1 項輸入", "%1 项输入", "入力 %1 件", "%1 Eingabe"},
    {"%1 tools", "%1 個工具", "%1 个工具", "ツール %1 件", "%1 Tools"},
    {"Raw", "原始", "原始", "生データ", "Rohdaten"},

    // Remaining UI strings (assistant list, session editor, settings, composer).
    {"%1 / %2 tok", "%1 / %2 tok", "%1 / %2 tok", "%1 / %2 tok", "%1 / %2 Tok"},
    {"(no output)", "（無輸出）", "（无输出）", "（出力なし）", "(keine Ausgabe)"},
    {"API protocol", "API 協議", "API 协议", "API プロトコル", "API-Protokoll"},
    {"Appearance and startup behaviour for the whole app.", "整個應用的外觀與啟動行為。", "整个应用的外观与启动行为。", "アプリ全体の外観と起動時の動作。", "Darstellung und Startverhalten für die gesamte App."},
    {"Chat Completions", "Chat Completions", "Chat Completions", "Chat Completions", "Chat Completions"},
    {"Choose an assistant", "選擇助手", "选择助手", "アシスタントを選択", "Assistenten auswählen"},
    {"Common", "通用", "通用", "共通", "Allgemein"},
    {"Compact threshold", "壓縮閾值", "压缩阈值", "圧縮しきい値", "Kompakt-Schwelle"},
    {"Configuration of the open assistant.", "當前開啟的助手的配置。", "当前打开的助手的配置。", "開いているアシスタントの設定。", "Konfiguration des geöffneten Assistenten."},
    {"Ctrl+Enter sends; Enter inserts a newline", "Ctrl+Enter 傳送；Enter 換行", "Ctrl+Enter 发送；Enter 换行", "Ctrl+Enter で送信、Enter で改行", "Strg+Eingabetaste sendet; Eingabetaste fügt einen Zeilenumbruch ein"},
    {"Custom Instructions", "自定義指令", "自定义指令", "カスタム指示", "Eigene Anweisungen"},
    {"Delete", "刪除", "删除", "削除", "Löschen"},
    {"Delete \"%1\" and its whole notebook? This cannot be undone.", "刪除“%1”及其整個筆記本？此操作無法撤銷。", "删除“%1”及其整个笔记本？此操作无法撤销。", "「%1」とそのノートブック全体を削除しますか？この操作は元に戻せません。", "„%1“ und das gesamte Notebook löschen? Dies kann nicht rückgängig gemacht werden."},
    {"Delete assistant", "刪除助手", "删除助手", "アシスタントを削除", "Assistent löschen"},
    {"Delete this session and its whole history? This cannot be undone.", "刪除此會話及其全部歷史？此操作無法撤銷。", "删除此会话及其全部历史？此操作无法撤销。", "このセッションとその履歴全体を削除しますか？この操作は元に戻せません。", "Diese Sitzung und ihren gesamten Verlauf löschen? Dies kann nicht rückgängig gemacht werden."},
    {"Directories this session can read and the tools it may use.", "此會話可讀取的目錄及可使用的工具。", "此会话可读取的目录及可使用的工具。", "このセッションが読み取れるディレクトリと使用できるツール。", "Verzeichnisse, die diese Sitzung lesen darf, und die Tools, die sie verwenden darf."},
    {"Each assistant is a separate notebook.", "每個助手是一個獨立的筆記本。", "每个助手是一个独立的笔记本。", "各アシスタントは独立したノートブックです。", "Jeder Assistent ist ein eigenes Notebook."},
    {"Extra guidance sent with every turn of this session.", "隨此會話每一回合一同傳送的額外指引。", "随此会话每一回合一同发送的额外指引。", "このセッションの毎ターンに送信される追加の指針。", "Zusätzliche Anweisungen, die bei jeder Runde dieser Sitzung mitgesendet werden."},
    {"Go to the previous prompt", "跳到上一條提示", "跳到上一条提示", "前のプロンプトへ移動", "Zum vorherigen Prompt wechseln"},
    {"Local Notebook", "Local Notebook", "Local Notebook", "Local Notebook", "Local Notebook"},
    {"Local Notebook\nA local-first notebook for chat assistants.", "Local Notebook\n本地優先的聊天助手筆記本。", "Local Notebook\n本地优先的聊天助手笔记本。", "Local Notebook\nローカル優先のチャットアシスタント用ノートブック。", "Local Notebook\nEin lokales Notebook für Chat-Assistenten."},
    {"New", "新建", "新建", "新規", "Neu"},
    {"One directory per line", "每行一個目錄", "每行一个目录", "1行に1ディレクトリ", "Ein Verzeichnis pro Zeile"},
    {"One directory per line. They are mounted into the session's tools.", "每行一個目錄。它們會掛載到會話的工具中。", "每行一个目录。它们会挂载到会话的工具中。", "1行に1ディレクトリ。セッションのツールにマウントされます。", "Ein Verzeichnis pro Zeile. Sie werden in die Tools der Sitzung eingebunden."},
    {"Online (web search)", "線上（網路搜尋）", "在线（网络搜索）", "オンライン（ウェブ検索）", "Online (Websuche)"},
    {"Open this assistant automatically next time", "下次自動開啟此助手", "下次自动打开此助手", "次回このアシスタントを自動で開く", "Diesen Assistenten beim nächsten Mal automatisch öffnen"},
    {"Preserve tail", "保留尾部", "保留尾部", "末尾を保持", "Ende beibehalten"},
    {"Responses API", "Responses API", "Responses API", "Responses API", "Responses API"},
    {"Run commands", "執行命令", "运行命令", "コマンドを実行する", "Befehle ausführen"},
    {"Select a session, or create a new one.", "選擇一個會話，或新建一個。", "选择一个会话，或新建一个。", "セッションを選択するか、新規作成します。", "Wählen Sie eine Sitzung oder erstellen Sie eine neue."},
    {"Summary budget", "摘要預算", "摘要预算", "要約バジェット", "Budget der Zusammenfassung"},
    {"Title, reasoning and the budgets this session runs with.", "此會話使用的標題、推理與預算。", "此会话使用的标题、推理与预算。", "このセッションで使用するタイトル、推論、バジェット。", "Titel, Reasoning und die Budgets, mit denen diese Sitzung läuft."},
    {"Typography", "字型排印", "字体排印", "タイポグラフィ", "Typografie"},
    {"Untitled", "未命名", "未命名", "無題", "Unbenannt"},
};

const std::unordered_map<std::string_view, Entry>& table() {
  static const std::unordered_map<std::string_view, Entry> map = [] {
    std::unordered_map<std::string_view, Entry> built;
    built.reserve(std::size(kTranslations));
    for (const Entry& entry : kTranslations) {
      built.emplace(entry.en, entry);
    }
    return built;
  }();
  return map;
}

Language g_language = Language::English;

QString from_utf8(std::string_view text) {
  return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

// The column for one language, or null for a language that needs no entry.
const char* translated(const Entry& entry, Language language) {
  switch (language) {
  case Language::ChineseSimplified:
    return entry.zh_hans;
  case Language::ChineseTraditional:
    return entry.zh_hant;
  case Language::Japanese:
    return entry.ja;
  case Language::German:
    return entry.de;
  case Language::English:
    break;
  }
  return nullptr;
}

} // namespace

Language language_from_code(std::string_view code) {
  // Locale codes first; the bare legacy codes and the region-less ISO forms are
  // accepted so a hand-edited settings file always selects a real language.
  if (code == "zh_cn" || code == "zh_hans" || code == "zh") {
    return Language::ChineseSimplified;
  }
  if (code == "zh_tw" || code == "zh_hk" || code == "zh_hant") {
    return Language::ChineseTraditional;
  }
  if (code == "ja_jp" || code == "ja") {
    return Language::Japanese;
  }
  if (code == "de_de" || code == "de") {
    return Language::German;
  }
  return Language::English;
}

std::string_view code_from_language(Language language) {
  switch (language) {
  case Language::ChineseSimplified:
    return "zh_cn";
  case Language::ChineseTraditional:
    return "zh_tw";
  case Language::Japanese:
    return "ja_jp";
  case Language::German:
    return "de_de";
  case Language::English:
    break;
  }
  return "en_us";
}

QString language_name(Language language) {
  switch (language) {
  case Language::ChineseSimplified:
    return QString::fromUtf8("\u7b80\u4f53\u4e2d\u6587");
  case Language::ChineseTraditional:
    return QString::fromUtf8("\u7e41\u9ad4\u4e2d\u6587");
  case Language::Japanese:
    return QString::fromUtf8("\u65e5\u672c\u8a9e");
  case Language::German:
    return QStringLiteral("Deutsch");
  case Language::English:
    break;
  }
  return QStringLiteral("English");
}

void set_language(Language language) { g_language = language; }

Language language() { return g_language; }

QString translate(std::string_view source) {
  if (g_language == Language::English) {
    return from_utf8(source);
  }

  const auto found = table().find(source);
  if (found == table().end()) {
    return from_utf8(source);
  }

  const char* value = translated(found->second, g_language);
  return value != nullptr ? QString::fromUtf8(value) : from_utf8(source);
}

} // namespace lnb::i18n
