import 'dart:convert';
import 'package:http/http.dart' as http;
import 'package:flutter_dotenv/flutter_dotenv.dart';

class GeminiService {
  static const String _baseUrl = 'https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent';

  Future<GeminiResponse> performGroundedSearch(String userQuery) async {
    final apiKey = dotenv.env['GEMINI_API_KEY'];
    if (apiKey == null || apiKey.isEmpty) {
      throw Exception('GEMINI_API_KEY is missing in .env');
    }

    final url = Uri.parse('$_baseUrl?key=$apiKey');

    final response = await http.post(
      url,
      headers: {'Content-Type': 'application/json'},
      body: jsonEncode({
        "contents": [
          {
            "parts": [
              {"text": userQuery}
            ]
          }
        ],
        // This tool triggers the "Google Search" behavior server-side
        "tools": [
          {"google_search": {}}
        ]
      }),
    );

    if (response.statusCode == 200) {
      return _parseResponse(jsonDecode(response.body));
    } else {
      throw Exception('API Error: ${response.statusCode} - ${response.body}');
    }
  }

  GeminiResponse _parseResponse(Map<String, dynamic> json) {
    // 1. Extract the main text answer
    final candidate = json['candidates']?[0];
    final contentParts = candidate?['content']?['parts'] as List?;
    String answer = "No response generated.";
    
    if (contentParts != null && contentParts.isNotEmpty) {
      answer = contentParts[0]['text'] ?? "No text found.";
    }

    // 2. Extract the "Grounding Metadata" (The Search Results)
    List<SearchResult> sources = [];
    final groundingMeta = candidate?['groundingMetadata'];
    
    if (groundingMeta != null) {
      final chunks = groundingMeta['groundingChunks'] as List?;
      if (chunks != null) {
        for (var chunk in chunks) {
          if (chunk.containsKey('web')) {
            sources.add(SearchResult(
              title: chunk['web']['title'],
              url: chunk['web']['uri'],
            ));
          }
        }
      }
    }

    return GeminiResponse(answer: answer, sources: sources);
  }
}

// Simple data models to hold the data for your UI
class GeminiResponse {
  final String answer;
  final List<SearchResult> sources;

  GeminiResponse({required this.answer, required this.sources});
}

class SearchResult {
  final String title;
  final String url;

  SearchResult({required this.title, required this.url});
}