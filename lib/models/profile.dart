class Profile {
  final String name;
  final String designation;
  final String department;
  final String university;
  final List<String> emails;
  final List<String> researchInterests;
  final List<String> education;
  final String location;

  Profile({
    required this.name,
    required this.designation,
    required this.department,
    required this.university,
    required this.emails,
    required this.researchInterests,
    required this.education,
    required this.location,
  });

  factory Profile.fromJson(Map<String, dynamic> json) {
    return Profile(
      name: json['name'] ?? '',
      designation: json['designation'] ?? '',
      department: json['department'] ?? '',
      university: json['university'] ?? '',
      emails: List<String>.from(json['contact_emails'] ?? []),
      researchInterests:
          List<String>.from(json['research_interests'] ?? []),
      education: List<String>.from(json['education'] ?? []),
      location: json['location'] ?? '',
    );
  }

  bool get isEmpty =>
      name.isEmpty &&
      designation.isEmpty &&
      department.isEmpty &&
      university.isEmpty &&
      emails.isEmpty &&
      researchInterests.isEmpty &&
      education.isEmpty &&
      location.isEmpty;
}
