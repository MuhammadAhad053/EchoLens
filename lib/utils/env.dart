// lib/utils/env.dart
import 'package:flutter_dotenv/flutter_dotenv.dart';

class Env {
  static String get geminiKey => dotenv.env['GEMINI_API_KEY']!;
  static String get googleKey => dotenv.env['GOOGLE_CSE_KEY']!;
  static String get googleCx => dotenv.env['GOOGLE_CX']!;
}
