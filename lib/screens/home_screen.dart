import 'dart:async';
import 'dart:io';
import 'package:flutter/material.dart';
import 'package:url_launcher/url_launcher.dart';
import 'package:flutter_markdown_plus/flutter_markdown_plus.dart';
import 'package:firebase_auth/firebase_auth.dart';
import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:pdf/pdf.dart';
import 'package:pdf/widgets.dart' as pw;
import 'package:printing/printing.dart';
import 'package:htmltopdfwidgets/htmltopdfwidgets.dart' as hp;
import 'package:flutter_dotenv/flutter_dotenv.dart';

import '../services/gemini_service.dart';

class GroundingSearchScreen extends StatefulWidget {
  const GroundingSearchScreen({super.key});

  @override
  State<GroundingSearchScreen> createState() => _GroundingSearchScreenState();
}

class _GroundingSearchScreenState extends State<GroundingSearchScreen> {
  final TextEditingController _controller = TextEditingController();
  // New controller for history search
  final TextEditingController _historySearchController = TextEditingController();
  final GeminiService _geminiService = GeminiService();
  
  bool _isLoading = false;
  GeminiResponse? _response;
  String? _errorMessage;
  String _displayName = "User";
  String _historyFilter = ""; // State variable for search text

  // --- LIMIT VARIABLES ---
  static const int _dailyLimit = 3;
  static final String _adminEmail = dotenv.env['ADMIN_EMAIL'] ?? ''; // REPLACE WITH YOUR EMAIL
  int _searchesUsedToday = 0;
  bool _isExempt = false;

  @override
  void initState() {
    super.initState();
    _fetchUserData();
    // Listen to changes in the history search bar
    _historySearchController.addListener(() {
      setState(() {
        _historyFilter = _historySearchController.text.toLowerCase();
      });
    });
  }

  @override
  void dispose() {
    _controller.dispose();
    _historySearchController.dispose();
    super.dispose();
  }

Future<void> _fetchUserData() async {
    final user = FirebaseAuth.instance.currentUser;
    if (user != null) {
      // Check exemption immediately
      setState(() {
        _isExempt = user.email == _adminEmail;
      });

      try {
        final doc = await FirebaseFirestore.instance
            .collection('users')
            .doc(user.uid)
            .get();
        
        if (doc.exists && doc.data() != null) {
          final data = doc.data()!;
          setState(() {
            _displayName = data['username'] ?? user.email?.split('@')[0] ?? "User";
            
            // Check and load search limit data
            final lastSearchTimestamp = data['lastSearchDate'] as Timestamp?;
            if (lastSearchTimestamp != null) {
              final lastSearchDate = lastSearchTimestamp.toDate();
              final now = DateTime.now();
              // Reset if it's a new day
              if (lastSearchDate.year != now.year || 
                  lastSearchDate.month != now.month || 
                  lastSearchDate.day != now.day) {
                _searchesUsedToday = 0;
                // Update Firestore reset immediately to keep it clean
                FirebaseFirestore.instance.collection('users').doc(user.uid).update({
                  'searchesUsedToday': 0,
                  'lastSearchDate': FieldValue.serverTimestamp(),
                });
              } else {
                _searchesUsedToday = data['searchesUsedToday'] ?? 0;
              }
            }
          });
        }
      } catch (e) {
        debugPrint("Error fetching user data: $e");
      }
    }
  }

  Future<void> _updateUsageCount() async {
    final user = FirebaseAuth.instance.currentUser;
    if (user == null || _isExempt) return;

    final newCount = _searchesUsedToday + 1;
    
    // Optimistic UI update
    setState(() => _searchesUsedToday = newCount);

    try {
      await FirebaseFirestore.instance.collection('users').doc(user.uid).update({
        'searchesUsedToday': newCount,
        'lastSearchDate': FieldValue.serverTimestamp(),
      });
    } catch (e) {
      debugPrint("Failed to update usage count: $e");
    }
  }

  /// Logic to delete the account and its associated Firestore data
  Future<void> _handleDeleteAccount() async {
    final user = FirebaseAuth.instance.currentUser;
    if (user == null) return;

    final confirm = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        backgroundColor: const Color(0xFF2C2C2C),
        title: const Text("Delete Account?", style: TextStyle(color: Colors.white)),
        content: const Text(
          "This will permanently delete your profile, research history, and credentials. This cannot be undone.",
          style: TextStyle(color: Colors.white70),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: const Text("Cancel"),
          ),
          TextButton(
            onPressed: () => Navigator.pop(context, true),
            child: const Text("Delete Everything", style: TextStyle(color: Colors.redAccent)),
          ),
        ],
      ),
    );

    if (confirm == true) {
      setState(() => _isLoading = true);
      try {
        final uid = user.uid;

        // 1. Delete History Subcollection
        final historyDocs = await FirebaseFirestore.instance
            .collection('users')
            .doc(uid)
            .collection('history')
            .get();
        
        final batch = FirebaseFirestore.instance.batch();
        for (var doc in historyDocs.docs) {
          batch.delete(doc.reference);
        }
        
        // 2. Delete User Document
        batch.delete(FirebaseFirestore.instance.collection('users').doc(uid));
        await batch.commit();

        // 3. Delete Authentication Account
        // Note: If the user hasn't logged in recently, this might throw a 
        // 'requires-recent-login' error. In a production app, you'd re-authenticate here.
        await user.delete();

        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            const SnackBar(content: Text("Account and data permanently deleted.")),
          );
        }
      } catch (e) {
        debugPrint("Deletion error: $e");
        if (mounted) {
          setState(() {
            _isLoading = false;
            _errorMessage = "Security: Please log out and log back in before deleting your account.";
          });
        }
      }
    }
  }

  void _handleError(Object e) {
    debugPrint("Search Error Trace: $e");
    if (!mounted) return;
    setState(() {
      _isLoading = false; 
      final errorString = e.toString();
      if (e is SocketException) {
        _errorMessage = "No internet connection. Please check your network.";
      } else if (e is TimeoutException) {
        _errorMessage = "Request timed out. Gemini is taking too long.";
      } else if (errorString.contains('429')) {
        _errorMessage = "API limit reached. Please wait a moment.";
      } else if (errorString.contains('401') || errorString.contains('403')) {
        _errorMessage = "Invalid API Key. Check .env file.";
      } else {
        _errorMessage = "Error: $errorString";
      }
    });
  }

  Future<void> _saveToHistory(String query, GeminiResponse response) async {
    final user = FirebaseAuth.instance.currentUser;
    if (user == null) return;

    try {
      // Map sources to a list of maps for Firestore
      final sourcesData = response.sources.map((s) => {
        'title': s.title,
        'url': s.url
      }).toList();

      await FirebaseFirestore.instance
          .collection('users')
          .doc(user.uid)
          .collection('history')
          .add({
        'query': query,
        'answer': response.answer,
        'sources': sourcesData,
        'timestamp': FieldValue.serverTimestamp(),
      });
    } catch (e) {
      debugPrint("Failed to save history: $e");
    }
  }

  Future<void> _deleteHistoryItem(String docId) async {
    final user = FirebaseAuth.instance.currentUser;
    if (user == null) return;
    try {
      await FirebaseFirestore.instance
          .collection('users')
          .doc(user.uid)
          .collection('history')
          .doc(docId)
          .delete();
    } catch (e) {
      debugPrint("Failed to delete item: $e");
    }
  }

  Future<void> _clearAllHistory() async {
    final user = FirebaseAuth.instance.currentUser;
    if (user == null) return;
    
    final confirm = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text("Clear History",
          style: TextStyle(
            color: Colors.white,
          ),
        ),
        content: const Text("Are you sure you want to delete all search records?",
          style: TextStyle(
            color: Colors.white,
          ),
        ),
        actions: [
          TextButton(onPressed: () => Navigator.pop(context, false), child: const Text("Cancel")),
          TextButton(onPressed: () => Navigator.pop(context, true), child: const Text("Clear All", style: TextStyle(color: Colors.red))),
        ],
      ),
    );

    if (confirm == true) {
      try {
        final collection = FirebaseFirestore.instance
            .collection('users')
            .doc(user.uid)
            .collection('history');
        final snapshots = await collection.get();
        for (var doc in snapshots.docs) {
          await doc.reference.delete();
        }
      } catch (e) {
        debugPrint("Failed to clear history: $e");
      }
    }
  }

  void _loadFromHistory(Map<String, dynamic> data) {
    setState(() {
      _controller.text = data['query'] ?? '';
      _errorMessage = null;
      
      // Reconstruct GeminiResponse from saved data
      final List<dynamic> sourceMaps = data['sources'] ?? [];
      final List<SearchResult> reconstructedSources = sourceMaps.map((s) => 
        SearchResult(title: s['title'] ?? '', url: s['url'] ?? '')
      ).toList();

      _response = GeminiResponse(
        answer: data['answer'] ?? '',
        sources: reconstructedSources,
      );
    });
    Navigator.of(context).pop(); // Close drawer
  }

Future<void> _performSearch() async {
    final String query = _controller.text.trim();
    if (query.isEmpty) return;

    // --- LIMIT CHECK ---
    if (!_isExempt && _searchesUsedToday >= _dailyLimit) {
      showDialog(
        context: context,
        builder: (context) => AlertDialog(
          backgroundColor: const Color(0xFF2C2C2C),
          title: const Text("Daily Limit Reached", style: TextStyle(color: Colors.white)),
          content: const Text(
            "You have used your 3 free searches for today. Please try again tomorrow or contact support.",
            style: TextStyle(color: Colors.white70),
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context),
              child: const Text("OK", style: TextStyle(color: Colors.redAccent)),
            ),
          ],
        ),
      );
      return;
    }

    setState(() {
      _isLoading = true;
      _errorMessage = null;
      _response = null;
    });

    try {
      try {
        FocusManager.instance.primaryFocus?.unfocus();
      } catch (uiErr) {
        debugPrint("UI Cleanup non-fatal error: $uiErr");
      }

      final profilePrompt = """
        Conduct a web search for the professional profile of: $query
        Extract the following details and present them in a clean, readable format:
        - Full Name
        - Current Designation/Job Title
        - Department
        - University or Affiliation
        - Contact Emails (list all found)
        - Research Interests or Key Achievements
        - Education History
        - Location

        RULES:
        1. Each field's heading MUST be bold using double asterisks and followed by a colon, e.g., "**Full Name**: John Doe". 
        2. After each field, have a new line for better formatting.
        3. DO NOT include introductory lines like "Here is the professional profile..." or "Based on my research".
        4. Provide ONLY the required details and nothing else.
        5. Finally, provide a short professional summary with the specific heading "**Summary**".
      """;

      final result = await _geminiService.performGroundedSearch(profilePrompt).timeout(const Duration(seconds: 45)); 
      await _saveToHistory(query, result);
      await _updateUsageCount();

      if (mounted) {
        setState(() {
          _response = result;
          _isLoading = false;
        });
      }
    } catch (e) {
      _handleError(e);
    } finally {
      if (mounted && _isLoading) {
        setState(() => _isLoading = false);
      }
    }
  }

  String _convertToHtml(String markdown) {
    RegExp boldExp = RegExp(r'\*\*(.*?)\*\*');
    String html = markdown.replaceAllMapped(boldExp, (match) => '<b>${match.group(1)}</b>');
    List<String> lines = html.split('\n');
    List<String> processedLines = [];
    for (var line in lines) {
      String l = line.trim();
      if (l.isEmpty) {
        processedLines.add('<br/>');
      } else {
        processedLines.add('<div>$l</div>');
      }
    }
    return processedLines.join('');
  }

  Future<void> _generateAndDownloadPdf() async {
    if (_response == null) return;
    try {
      final pdf = pw.Document();
      final htmlContent = _convertToHtml(_response!.answer);
      final pdfWidgets = await hp.HTMLToPdf().convert(htmlContent);

      pdf.addPage(
        pw.MultiPage(
          pageFormat: PdfPageFormat.a4,
          margin: const pw.EdgeInsets.all(32),
          build: (pw.Context context) {
            return [
              pw.Header(
                level: 0,
                child: pw.Row(
                  mainAxisAlignment: pw.MainAxisAlignment.spaceBetween,
                  children: [
                    pw.Text("EchoLens Profile Report", 
                      style: pw.TextStyle(fontSize: 22, fontWeight: pw.FontWeight.bold, color: PdfColors.amber800)),
                    pw.Text(DateTime.now().toString().split(' ')[0], 
                      style: const pw.TextStyle(fontSize: 10, color: PdfColors.grey)),
                  ],
                ),
              ),
              pw.SizedBox(height: 20),
              ...pdfWidgets,
              pw.SizedBox(height: 30),
              pw.Divider(thickness: 0.5, color: PdfColors.grey300),
              pw.Align(
                alignment: pw.Alignment.centerRight,
                child: pw.Text("Generated by EchoLens", 
                  style: const pw.TextStyle(fontSize: 8, color: PdfColors.grey)),
              ),
            ];
          },
        ),
      );

      await Printing.layoutPdf(
        onLayout: (PdfPageFormat format) async => pdf.save(),
        name: 'Profile_${_controller.text.replaceAll(' ', '_')}.pdf',
      );
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text("PDF Error: $e")));
      }
    }
  }

  Future<void> _launchURL(String url) async {
    try {
      final uri = Uri.parse(url);
      if (!await launchUrl(uri, mode: LaunchMode.externalApplication)) {
         throw 'Could not launch $url';
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('Error: $e')));
      }
    }
  }

    // --- HIGHLIGHTING LOGIC ---
  Widget _buildHighlightedText(String text, String query, Color brandColor) {
    if (query.isEmpty) {
      return Text(text, style: const TextStyle(color: Colors.white, fontSize: 13));
    }

    final List<TextSpan> spans = [];
    final String lowerText = text.toLowerCase();
    final String lowerQuery = query.toLowerCase();
    int start = 0;
    int indexOfHighlight;

    while ((indexOfHighlight = lowerText.indexOf(lowerQuery, start)) != -1) {
      if (indexOfHighlight > start) {
        spans.add(TextSpan(text: text.substring(start, indexOfHighlight)));
      }
      spans.add(TextSpan(
        text: text.substring(indexOfHighlight, indexOfHighlight + query.length),
        style: TextStyle(
          color: Colors.white, 
          fontWeight: FontWeight.bold, 
          backgroundColor: brandColor.withValues(alpha: 0.5) // Highlight color
        ),
      ));
      start = indexOfHighlight + query.length;
    }

    if (start < text.length) {
      spans.add(TextSpan(text: text.substring(start)));
    }

    return Text.rich(
      TextSpan(
        style: const TextStyle(color: Colors.white, fontSize: 13),
        children: spans,
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    const brandColor = Color.fromARGB(255, 212, 160, 24);
    final user = FirebaseAuth.instance.currentUser;

    return Scaffold(
      backgroundColor: const Color(0xFF1C1C1C),
      drawer: Drawer(
        backgroundColor: const Color.fromARGB(255, 27, 27, 27),
        child: Column(
          children: [
            Container(
              width: double.infinity,
              // This padding replaces the SafeArea and SizedBox height constraint
              padding: EdgeInsets.only(
                top: MediaQuery.of(context).padding.top + 20, 
                bottom: 20,
                left: 16,
                right: 16,
              ),
              decoration: const BoxDecoration(
                color: Color.fromARGB(255, 36, 36, 36),
                border: Border(
                  bottom: BorderSide(
                    color: Colors.white24,
                    width: 1.0,
                  ),
                ),
              ),
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.center,
                children: [
                  CircleAvatar(
                    radius: 30,
                    backgroundColor: brandColor,
                    child: Text(
                      _displayName.isNotEmpty ? _displayName[0].toUpperCase() : "U",
                      style: const TextStyle(
                        color: Color(0xFF2C2C2C), 
                        fontSize: 24, 
                        fontWeight: FontWeight.w900
                      ),
                    ),
                  ),
                  const SizedBox(width: 14),
                  Expanded(
                    child: Column(
                      mainAxisSize: MainAxisSize.min, // Shrinks column to content height
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          _displayName.isNotEmpty ? _displayName : "EchoLens User",
                          style: const TextStyle(
                            color:  brandColor, 
                            fontWeight: FontWeight.bold, 
                            fontSize: 16
                          ),
                          overflow: TextOverflow.ellipsis,
                        ),
                        Text(
                          user?.email ?? "Guest",
                          style: const TextStyle(color: Colors.white70, fontSize: 13),
                          overflow: TextOverflow.ellipsis,
                        ),
                        if (!_isExempt)
                          Padding(
                            padding: const EdgeInsets.only(top: 4),
                            child: Text(
                              "Searches remaining: ${_dailyLimit - _searchesUsedToday}",
                              style: const TextStyle(color: Colors.white70, fontSize: 12, fontWeight: FontWeight.w600),
                            ),
                          ),
                        if (_isExempt)
                          const Padding(
                            padding: EdgeInsets.only(top: 4),
                            child: Text("Premium Account", style: TextStyle(color: Colors.white, fontSize: 12, fontWeight: FontWeight.bold)),
                          ),
                      ],
                    ),
                  ),
                ],
              ),
            ),
            // --- SEARCH HISTORY BAR ---
            Padding(
              padding: const EdgeInsets.fromLTRB(16, 12, 16, 0),
              child: TextField(
                controller: _historySearchController,
                style: const TextStyle(color: Colors.white, fontSize: 13),
                decoration: InputDecoration(
                  hintText: "Search history...",
                  hintStyle: const TextStyle(color: Colors.white24),
                  prefixIcon: const Icon(Icons.search, color: Colors.white24, size: 18),
                  filled: true,
                  fillColor: Color(0xFF2C2C2C),
                  isDense: true,
                  contentPadding: const EdgeInsets.symmetric(vertical: 8, horizontal: 12),
                  border: OutlineInputBorder(
                    borderRadius: BorderRadius.circular(8),
                    borderSide: BorderSide.none,
                  ),
                ),
              ),
            ),
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
              child: Row(
                mainAxisAlignment: MainAxisAlignment.spaceBetween,
                children: [
                  const Text("Past Researches", style: TextStyle(color: brandColor, fontSize: 16, fontWeight: FontWeight.bold)),
                  TextButton(
                    onPressed: _clearAllHistory,
                    child: const Text("Clear All", style: TextStyle(color: Colors.redAccent, fontSize: 12)),
                  ),
                ],
              ),
            ),
            Expanded(
              child: StreamBuilder<QuerySnapshot>(
                stream: FirebaseFirestore.instance
                    .collection('users')
                    .doc(user?.uid)
                    .collection('history')
                    .orderBy('timestamp', descending: true)
                    .limit(30)
                    .snapshots(),
                builder: (context, snapshot) {
                  if (snapshot.hasError) return const Center(child: Text("Error loading history", style: TextStyle(color: Colors.white54)));
                  if (snapshot.connectionState == ConnectionState.waiting) return const Center(child: CircularProgressIndicator(color: brandColor));
                  
                  final docs = snapshot.data?.docs ?? [];
                  if (docs.isEmpty) return const Center(child: Text("No search history yet", style: TextStyle(color: Colors.white54)));

                  // Filter documents locally based on search text
                  final filteredDocs = docs.where((doc) {
                    final data = doc.data() as Map<String, dynamic>;
                    final query = (data['query'] ?? '').toString().toLowerCase();
                    return query.contains(_historyFilter);
                  }).toList();

                  if (filteredDocs.isEmpty) {
                    return const Center(child: Text("No matching history found", style: TextStyle(color: Colors.white24)));
                  }

                  return ListView.builder(
                    padding: EdgeInsets.zero,
                    itemCount: filteredDocs.length,
                    itemBuilder: (context, index) {
                      final doc = filteredDocs[index];
                      final data = doc.data() as Map<String, dynamic>;
                      return ListTile(
                        leading: const Icon(Icons.description_outlined, color: brandColor, size: 20),
                        title: _buildHighlightedText(data['query'] ?? 'Unknown', _historyFilter, brandColor),
                        subtitle: Text(
                          data['timestamp'] != null 
                            ? (data['timestamp'] as Timestamp).toDate().toString().split(' ')[0]
                            : '', 
                          style: const TextStyle(color: Colors.white30, fontSize: 10)
                        ),
                        trailing: IconButton(
                          icon: const Icon(Icons.delete_outline, color: Colors.white24, size: 18),
                          onPressed: () => _deleteHistoryItem(doc.id),
                        ),
                        onTap: () => _loadFromHistory(data),
                      );
                    },
                  );
                },
              ),
            ),
            // DELETE ACCOUNT BUTTON (Placed above Logout)
            Container(
              decoration: const BoxDecoration(
                border: Border(
                  top: BorderSide(
                    color: Colors.white24,
                    width: 1.0,
                  ),
                ),
                color: Color.fromARGB(255, 36, 36, 36), 
              ),
              child: Material(
                type: MaterialType.transparency,
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    ListTile(
                      hoverColor: Colors.white.withValues(alpha: 0.5),
                      leading: const Icon(Icons.delete_forever, color: Colors.redAccent, size: 22),
                      title: const Text("Delete Account", style: TextStyle(color: Colors.redAccent, fontSize: 14)),
                      onTap: _handleDeleteAccount,
                    ),
                    ListTile(
                      hoverColor: Colors.white.withValues(alpha: 0.5), 
                      leading: const Icon(Icons.logout, color: Colors.white38),
                      title: const Text("Logout", style: TextStyle(color: Colors.white38)),
                      onTap: () async {
                        await FirebaseAuth.instance.signOut();
                      },
                    ),
                    SizedBox(height: MediaQuery.of(context).padding.bottom + 10),
                  ],
                ),
              ),
            ),
          ],
        ),
      ),
      appBar: AppBar(
        backgroundColor: const Color(0xFF1C1C1C),
        foregroundColor: brandColor,
        scrolledUnderElevation: 0,
        title: const Text("EchoLens", style: TextStyle(fontWeight: FontWeight.w900)),
        centerTitle: true,
        actions: [
          if (_response != null)
             IconButton(
              color: brandColor,
              icon: const Icon(Icons.picture_as_pdf),
              onPressed: _generateAndDownloadPdf,
              tooltip: "Export PDF",
            ),
        ],
      ),
      body: Padding(
        padding: const EdgeInsets.all(16.0),
        child: Column(
          children: [
            Row(
              children: [
                Expanded(
                  child: TextField(
                    controller: _controller,
                    decoration: InputDecoration(
                      hintText: "e.g. Dr. John Doe at MIT",
                      hintStyle: const TextStyle(color: Colors.white38),
                      enabledBorder: OutlineInputBorder(
                        borderRadius: BorderRadius.circular(12),
                        borderSide: const BorderSide(color: brandColor),
                      ),
                      focusedBorder: OutlineInputBorder(
                        borderRadius: BorderRadius.circular(12),
                        borderSide: const BorderSide(color: brandColor, width: 2),
                      ),
                      border: OutlineInputBorder(borderRadius: BorderRadius.circular(12)),
                    ),
                    style: const TextStyle(color: Colors.white),
                    cursorColor: brandColor,
                    onSubmitted: (_) => _isLoading ? null : _performSearch(),
                  ),
                ),
                const SizedBox(width: 12),
                SizedBox(
                  height: 50,
                  child: IconButton.filled(
                    onPressed: _isLoading ? null : () => _performSearch(),
                    icon: _isLoading
                        ? const SizedBox(width: 24, height: 24, child: CircularProgressIndicator(color: Colors.white, strokeWidth: 2))
                        : const Icon(Icons.search, color: Color(0xFF1C1C1C)),
                    style: IconButton.styleFrom(
                      backgroundColor: brandColor,
                      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                    ),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 20),
            if (_errorMessage != null)
              Card(
                color: Colors.red.shade900.withValues(alpha: 0.3),
                child: Padding(
                  padding: const EdgeInsets.all(12.0),
                  child: Row(
                    children: [
                      const Icon(Icons.error_outline, color: Colors.redAccent),
                      const SizedBox(width: 10),
                      Expanded(child: Text(_errorMessage!, style: const TextStyle(color: Colors.white))),
                    ],
                  ),
                ),
              ),
            Expanded(
              child: _isLoading 
                ? const Center(child: CircularProgressIndicator(color: brandColor))
                : (_response == null 
                    ? Center(
                        child: Column(
                          mainAxisAlignment: MainAxisAlignment.center,
                          children: [
                            const Icon(Icons.travel_explore, size: 64, color: Colors.white10),
                            const SizedBox(height: 16),
                            const Text("Ready for a new research?", style: TextStyle(color: Colors.white30)),
                          ],
                        ),
                      )
                    : ListView(
                        children: [
                          Row(
                            mainAxisAlignment: MainAxisAlignment.spaceBetween,
                            children: [
                              const Text("Profile Result", style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold, color: brandColor)),
                              IconButton(
                                icon: const Icon(Icons.close, color: Colors.white30),
                                onPressed: () => setState(() => _response = null),
                              )
                            ],
                          ),
                          Card(
                            color: const Color.fromARGB(255, 36, 36, 36),
                            shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                            child: Padding(
                              padding: const EdgeInsets.all(20.0),
                              child: MarkdownBody(
                                data: _response!.answer,
                                selectable: true,
                                styleSheet: MarkdownStyleSheet(
                                  p: const TextStyle(color: Color.fromARGB(255, 205, 205, 205), fontSize: 15, height: 1.6),
                                  strong: const TextStyle(color: Colors.white, fontWeight: FontWeight.bold),
                                ),
                              ),
                            ),
                          ),
                          const SizedBox(height: 20),
                          if (_response!.sources.isNotEmpty) ...[
                            const Text("Web Pages Scraped", style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold, color: brandColor)),
                            const SizedBox(height: 8),
                            ..._response!.sources.map((source) => ListTile(
                              contentPadding: EdgeInsets.zero,
                              leading: const Icon(Icons.link, color: brandColor, size: 18),
                              title: Text(source.title, style: const TextStyle(color: Colors.white70, fontSize: 14)),
                              onTap: () => _launchURL(source.url),
                            )),
                          ],
                        ],
                      )),
            ),
          ],
        ),
      ),
    );
  }
}